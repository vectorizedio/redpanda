# Copyright 2021 Vectorized, Inc.
#
# Use of this software is governed by the Business Source License
# included in the file licenses/BSL.md
#
# As of the Change Date specified in that file, in accordance with
# the Business Source License, use of this software will be governed
# by the Apache License, Version 2.0

import http.client
import json
import logging
import uuid
import requests
from ducktape.mark.resource import cluster
from ducktape.utils.util import wait_until

from rptest.clients.types import TopicSpec
from rptest.clients.kafka_cat import KafkaCat
from rptest.clients.kafka_cli_tools import KafkaCliTools
from rptest.tests.redpanda_test import RedpandaTest


def create_topic_names(count):
    return list(f"pandaproxy-topic-{uuid.uuid4()}" for _ in range(count))


HTTP_GET_HEADERS = {"Accept": "application/vnd.schemaregistry.v1+json"}

HTTP_POST_HEADERS = {
    "Accept": "application/vnd.schemaregistry.v1+json",
    "Content-Type": "application/vnd.schemaregistry.v1+json"
}


class SchemaRegistryTest(RedpandaTest):
    """
    Test schema registry against a redpanda cluster.
    """
    def __init__(self, context):
        super(SchemaRegistryTest, self).__init__(
            context,
            num_brokers=3,
            enable_pp=True,
            enable_sr=True,
            extra_rp_conf={"auto_create_topics_enabled": False})

        http.client.HTTPConnection.debuglevel = 1
        logging.basicConfig()
        requests_log = logging.getLogger("requests.packages.urllib3")
        requests_log.setLevel(logging.getLogger().level)
        requests_log.propagate = True

    def _base_uri(self):
        return f"http://{self.redpanda.nodes[0].account.hostname}:8081"

    def _get_topics(self):
        return requests.get(
            f"http://{self.redpanda.nodes[0].account.hostname}:8082/topics")

    def _create_topics(self,
                       names=create_topic_names(1),
                       partitions=1,
                       replicas=1,
                       cleanup_policy=TopicSpec.CLEANUP_DELETE):
        self.logger.debug(f"Creating topics: {names}")
        kafka_tools = KafkaCliTools(self.redpanda)
        for name in names:
            kafka_tools.create_topic(
                TopicSpec(name=name,
                          partition_count=partitions,
                          replication_factor=replicas))
        wait_until(lambda: set(names).issubset(self._get_topics().json()),
                   timeout_sec=30,
                   backoff_sec=1,
                   err_msg="Topics failed to settle")
        return names

    def _get_schemas_types(self, headers=HTTP_GET_HEADERS):
        return requests.get(f"{self._base_uri()}/schemas/types",
                            headers=headers)

    def _post_subjects_subject_versions(self,
                                        subject,
                                        data,
                                        headers=HTTP_POST_HEADERS):
        return requests.post(f"{self._base_uri()}/subjects/{subject}/versions",
                             headers=headers,
                             data=data)

    @cluster(num_nodes=3)
    def test_schemas_types(self):
        """
        Verify the schema registry returns the supported types
        """

        self.logger.debug(f"Request schema types with no accept header")
        result_raw = self._get_schemas_types(headers={})
        assert result_raw.status_code == requests.codes.ok

        self.logger.debug(f"Request schema types with defautl accept header")
        result_raw = self._get_schemas_types()
        assert result_raw.status_code == requests.codes.ok
        result = result_raw.json()
        assert result == ["AVRO"]

    @cluster(num_nodes=3)
    def test_post_subjects_subject_versions(self):
        """
        Verify posting a schema
        """

        self.logger.debug("Creating _schemas topic")
        self._create_topics(names=["_schemas"],
                            partitions=1,
                            replicas=1,
                            cleanup_policy=TopicSpec.CLEANUP_COMPACT)

        topic = create_topic_names(1)[0]

        self.logger.debug(f"Register a schema against a subject")
        schema_def = {
            "namespace":
            "example.avro",
            "type":
            "record",
            "name":
            "User",
            "fields": [{
                "name": "name",
                "type": "string"
            }, {
                "name": "favorite_number",
                "type": ["int", "null"]
            }, {
                "name": "favorite_color",
                "type": ["string", "null"]
            }]
        }

        schema_1_data = json.dumps({"schema": json.dumps(schema_def)})

        self.logger.debug("Posting schema 1 as a subject key")
        result_raw = self._post_subjects_subject_versions(
            subject=f"{topic}-key", data=schema_1_data)
        self.logger.debug(result_raw)
        assert result_raw.status_code == requests.codes.ok
        assert result_raw.json()["id"] == 1

        self.logger.debug("Reposting schema 1 as a subject key")
        result_raw = self._post_subjects_subject_versions(
            subject=f"{topic}-key", data=schema_1_data)
        self.logger.debug(result_raw)
        assert result_raw.status_code == requests.codes.ok
        assert result_raw.json()["id"] == 1

        self.logger.debug("Reposting schema 1 as a subject value")
        result_raw = self._post_subjects_subject_versions(
            subject=f"{topic}-value", data=schema_1_data)
        self.logger.debug(result_raw)
        assert result_raw.status_code == requests.codes.ok
        assert result_raw.json()["id"] == 1
