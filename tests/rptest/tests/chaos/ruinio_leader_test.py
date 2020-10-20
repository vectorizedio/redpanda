import sys
sys.path.append("/opt/v/src/consistency-testing/gobekli")
sys.path.append("/opt/v/src/consistency-testing/chaostest")
from chaostest.faults import RuinIORecoverableFault
from rptest.tests.chaos.chaos_base_test import BaseChaosTest
from ducktape.mark.resource import cluster


class RuinIOLeaderTest(BaseChaosTest):
    def __init__(self, test_context, num_nodes=3):
        super(RuinIOLeaderTest, self).__init__(test_context, num_nodes)

    @cluster(num_nodes=3)
    def test_ruinio_leader(self):
        failure_factory = lambda: RuinIORecoverableFault(
            lambda x: x.get_leader(), "leader")
        self.run(failure_factory)
