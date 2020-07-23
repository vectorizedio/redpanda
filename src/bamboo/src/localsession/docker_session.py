import io
import os
import tempfile
import shutil
import time
from urllib.parse import urlparse
from typing import List

# 3rd party
import docker
from docker.models.containers import Container
from docker.types import IPAMConfig, IPAMPool, Mount

import petname
from jinja2 import Template
from absl import logging

# our packages
from .. import session

_BUILD_TEMPLATE = """# bamboo autogenerated image
from fedora:30

# basedirs
RUN mkdir -p /var/lib/redpanda/data \
             /var/lib/redpanda/conf \
             /opt/redpanda \
             /etc/redpanda

# vars
EXPOSE 9092 33145 9644
ENV PATH $PATH:/opt/redpanda/bin

# non-existent (yet) links
RUN ln -sf /opt/redpanda/etc/admin-api-doc /etc/redpanda/admin-api-doc
ADD redpanda.tar.gz /opt/redpanda
ADD redpanda.yaml /etc/redpanda/redpanda.yaml
# bug w/ rpk
# CMD ["/opt/redpanda/bin/rpk", "start", "--install-dir", "/opt/redpanda"]
# workaround:
CMD ["/opt/redpanda/bin/redpanda", "--redpanda-cfg", "/etc/redpanda/redpanda.yaml", "-c", "1"]
"""

_JINJA_CONFIG_TEMPLATE = """# bamboo autogen config
organization: "vectorized"
cluster_id: "{{cluster}}"

redpanda:
  developer_mode: true
  auto_create_topics_enabled: true
  data_directory: "/var/lib/redpanda/data"
  node_id: {{node_id}}
  rpc_server:
    address: "{{node_ip}}"
    port: 33145
  kafka_api:
    address: "{{node_ip}}"
    port: 9092
  admin:
    address: "{{node_ip}}"
    port: 9644

  # The initial cluster nodes addresses
{% if seed_servers is defined and seed_servers|length > 0 %}
  seed_servers:
  {% for seed in seed_servers %}
    - host:
        address: {{seed.ip}}
        port: 33145
      node_id: {{seed.node_id}}
  {% endfor %}
{% else %}
  seed_servers: []
{% endif %}

rpk:
  enable_usage_stats: false
  tune_network: false
  tune_disk_scheduler: false
  tune_disk_nomerges: false
  tune_disk_irq: false
  tune_fstrim: false
  tune_cpu: false
  tune_aio_events: false
  tune_clocksource: false
  tune_swappiness: false
  enable_memory_locking: false
  tune_coredump: false
  coredump_dir: "/var/lib/redpanda/coredump"
"""


def _ip_for_node_id(node_id):
    return f"172.16.5.{node_id}"


class RedpandaNode:
    def __init__(self, node_id, ip):
        self.node_id = node_id
        self.ip = ip


class DockerSessionState:
    def __init__(self, network_name):
        self.network_name = network_name
        self.nodes = {}
        self.containers = {}

    def add_node(self, redpanda_node: RedpandaNode) -> None:
        self.nodes[redpanda_node.node_id] = redpanda_node

    def add_container(self, container: Container) -> None:
        self.containers[container.id] = container
        node_id = int(container.labels["node_id"])
        if not self.contains(node_id):
            self.add_node(RedpandaNode(node_id, _ip_for_node_id(node_id)))

    def size(self) -> int:
        return len(self.nodes)

    def is_empty(self) -> bool:
        return self.size() == 0

    def contains(self, node_id) -> bool:
        return node_id in self.nodes

    def nodelist(self) -> List[RedpandaNode]:
        return list(self.nodes.values())

    def containerlist(self) -> List[Container]:
        return list(self.containers.values())


class DockerSession(session.Session):
    def __init__(self, session_name):
        self._state = DockerSessionState(session_name)
        self._client = docker.from_env()
        self._create_network()
        self._recover_state()

    def add_node(self, node_id, uri_package):
        if node_id is None:
            raise Exception("Invalid node_id. cannot add node")

        # we use the node id as the last octect of the ipv4
        rounded_node_id = self._round_node_id(node_id)
        if self._state.contains(rounded_node_id):
            raise Exception("Tried adding the same node multiple times."
                            f" Invalid id:{node_id} or {rounded_node_id}")
        node_id = rounded_node_id
        pkg = urlparse(uri_package)
        if pkg.scheme != "file":
            raise Exception(
                f"We can only parse file:/// schemes. {uri_package} is invalid"
            )
        target_mem_dir = "/dev/shm"
        if not os.path.exists("/dev/shm"):
            logging.info("cannot find /dev/shm for staging build")
            target_mem_dir = None
        with tempfile.TemporaryDirectory(prefix="bamboo.",
                                         dir=target_mem_dir) as dirname:
            logging.info(f"rendering configuration in {dirname}/redpanda.yaml")
            with open(os.path.join(dirname, "redpanda.yaml"), 'w') as cfg:
                contents = self._render_config(node_id)
                logging.info(f"\n\n{contents}\n\n")
                cfg.write(contents)

            logging.info(
                f"copying redpanda tarbal from: {pkg.path} -> {dirname}/redpanda.tar.gz"
            )
            shutil.copyfile(pkg.path, os.path.join(dirname, "redpanda.tar.gz"))

            logging.info(f"rendering dockerfile in {dirname}")
            dockerfile = str(os.path.join(dirname, "Dockerfile"))
            with open(dockerfile, 'w') as buildfile:
                buildfile.write(_BUILD_TEMPLATE)

            logging.info(
                f"building image with tag:{self._docker_image_tag(node_id)}")
            image = self._client.images.build(
                path=dirname,
                tag=self._docker_image_tag(node_id),
                labels={"node_id": str(node_id)})

            self._add_node(node_id)
        self._run_container(node_id)

    def remove_node(self, node_id):
        pass

    def isolate_node(self, node_id):
        pass

    def tcp_traffic_control(self, node_id):
        pass

    def destroy(self, node_id):
        if node_id is None:
            for c in self._state.containerlist():
                self._destroy_one(c)
            net = self._client.networks.get(self._network_id)
            logging.info(f"removing network: id={self._network_id}"
                         f" name={self._state.network_name}")
            net.remove()
            return

        snode_id = str(node_id)
        for c in self._state.containerlist():
            if c.labesl["node_id"] == snode_id:
                self._destroy_one(c)
                return
        # nothing to do
        logging.error(f"Could not find node:{snode_id}")

    def _destroy_one(self, container):
        logging.info(
            f"destroying container id={container.short_id} name={container.name}"
        )
        container.stop()
        container.remove()

    def run_test(self, v_path, test_path):
        # simple heuristic test name
        path = os.path.abspath(test_path)
        tag = os.path.basename(path)

        self._client.images.build(path=path, tag=tag)

        # broker list from session
        env = dict()
        brokers = ",".join(
            map(lambda n: f"{n.ip}:9092", self._state.nodelist()))
        args = f"--brokers {brokers}"
        env["BAMBOO_BROKERS"] = brokers
        env["BAMBOO_BROKER_COUNT"] = len(self._state.nodelist())

        # unique test namespace
        ns = f"bamboo-{tag}-{int(time.time())}"
        args = f"{args} --namespace {ns}"
        env["BAMBOO_NAMESPACE"] = ns

        # mount v read-only for convenience such as accessing scripts
        mounts = [Mount("/opt/v", str(v_path), type="bind", read_only=True)]

        try:
            container = self._client.containers.run(tag,
                                                    command=args,
                                                    network=self._network_id,
                                                    mounts=mounts,
                                                    environment=env,
                                                    stderr=True,
                                                    detach=True)
            for line in container.logs(stream=True):
                logging.info(f"{tag} test: {line.strip()}")
            res = container.wait()
            exit_code = res["StatusCode"]
            if exit_code == 0:
                logging.info("PASS")
            else:
                logging.info(f"FAIL exit_code {exit_code}")
        except:
            logging.info("FAIL")
            raise

    def _round_node_id(self, node_id):
        n = node_id % 255
        if n == 0: return 1
        return n

    def _run_container(self, node_id):
        # not necessary, but verifies we have correct internal state
        n = self._get_node(node_id)
        logging.info(f"attempting to run node: id={n.node_id},ip={n.ip}")
        container = self._client.containers.run(self._docker_image_tag(
            n.node_id),
                                                detach=True)
        self._state.add_container(container)
        net = self._client.networks.get(self._network_id)
        net.connect(container, ipv4_address=_ip_for_node_id(node_id))
        logging.info(f"starting container: {container.short_id} detached=True")
        container.start()

    def _get_node(self, node_id):
        if not self._state.contains(node_id):
            raise Exception(f"missing node: {node_id}")
        return self._state.nodes[node_id]

    def _add_node(self, node_id):
        self._state.add_node(RedpandaNode(node_id, _ip_for_node_id(node_id)))
        logging.info(f"udpating ORM with new node configuration for {node_id}")

    def _docker_image_tag(self, node_id) -> str:
        return f"{self._state.network_name}.{node_id}"

    def _create_network(self):
        networks_by_name = self._client.networks.list()
        for n in networks_by_name:
            logging.info(f"network {n.name}:{n.short_id}")
            if n.name == self._state.network_name:
                self._network_id = n.id
                return

        n = self._client.networks.create(
            name=self._state.network_name,
            check_duplicate=True,
            attachable=True,
            ipam=IPAMConfig(driver='default',
                            pool_configs=[
                                IPAMPool(subnet="172.16.0.0/16",
                                         iprange="172.16.5.0/24",
                                         gateway="172.16.5.254")
                            ]))
        self._network_id = n.id

    def _recover_state(self):
        if not self._network_id:
            raise Exception("docker network not initialized")

        n = self._client.networks.get(self._network_id)
        for c in n.containers:
            logging.info(f"Found container: {c}")
            self._state.add_container(c)

    def _render_config(self, node_id):
        tpl = Template(_JINJA_CONFIG_TEMPLATE)
        return tpl.render(node_id=node_id,
                          node_ip=_ip_for_node_id(node_id),
                          cluster=self._state.network_name,
                          seed_servers=self._state.nodelist())


def generate_docker_session(name=None):
    if name is None:
        name = "bamboo.%s" % petname.Generate(2, ".", 6)
    logging.info(f"starting session: {name}")
    return DockerSession(name)
