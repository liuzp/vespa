// Copyright 2017 Yahoo Holdings. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.
package com.yahoo.vespa.hosted.provision.maintenance;

import com.yahoo.config.provision.ActivationContext;
import com.yahoo.config.provision.ApplicationId;
import com.yahoo.config.provision.ApplicationName;
import com.yahoo.config.provision.ApplicationTransaction;
import com.yahoo.config.provision.Capacity;
import com.yahoo.config.provision.ClusterResources;
import com.yahoo.config.provision.ClusterSpec;
import com.yahoo.config.provision.Deployer;
import com.yahoo.config.provision.InstanceName;
import com.yahoo.config.provision.NodeResources;
import com.yahoo.config.provision.TenantName;
import com.yahoo.test.ManualClock;
import com.yahoo.vespa.hosted.provision.Node;
import com.yahoo.vespa.hosted.provision.NodeRepository;
import com.yahoo.vespa.hosted.provision.provisioning.NodeRepositoryProvisioner;
import com.yahoo.vespa.hosted.provision.provisioning.ProvisioningTester;
import com.yahoo.vespa.hosted.provision.testutils.MockDeployer;
import com.yahoo.vespa.orchestrator.OrchestrationException;
import com.yahoo.vespa.orchestrator.Orchestrator;
import org.junit.Before;
import org.junit.Test;

import java.time.Duration;
import java.util.Collections;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.doNothing;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;

/**
 * @author bratseth
 */
public class RetiredExpirerTest {

    private final NodeResources hostResources = new NodeResources(64, 128, 2000, 10);
    private final NodeResources nodeResources = new NodeResources(2, 8, 50, 1);

    private final ProvisioningTester tester = new ProvisioningTester.Builder().build();
    private final ManualClock clock = tester.clock();
    private final NodeRepository nodeRepository = tester.nodeRepository();
    private final NodeRepositoryProvisioner provisioner = tester.provisioner();
    private final Orchestrator orchestrator = mock(Orchestrator.class);

    private static final Duration RETIRED_EXPIRATION = Duration.ofHours(12);

    @Before
    public void setup() throws OrchestrationException {
        // By default, orchestrator should deny all request for suspension so we can test expiration
        doThrow(new RuntimeException()).when(orchestrator).acquirePermissionToRemove(any());
    }

    @Test
    public void ensure_retired_nodes_time_out() {
        tester.makeReadyNodes(7, nodeResources);
        tester.makeReadyHosts(4, hostResources);

        ApplicationId applicationId = ApplicationId.from(TenantName.from("foo"), ApplicationName.from("bar"), InstanceName.from("fuz"));

        // Allocate content cluster of sizes 7 -> 2 -> 3:
        // Should end up with 3 nodes in the cluster (one previously retired), and 4 retired
        ClusterSpec cluster = ClusterSpec.request(ClusterSpec.Type.content, ClusterSpec.Id.from("test")).vespaVersion("6.42").build();
        int wantedNodes;
        activate(applicationId, cluster, wantedNodes=7, 1);
        activate(applicationId, cluster, wantedNodes=2, 1);
        activate(applicationId, cluster, wantedNodes=3, 1);
        assertEquals(7, nodeRepository.getNodes(applicationId, Node.State.active).size());
        assertEquals(0, nodeRepository.getNodes(applicationId, Node.State.inactive).size());

        // Cause inactivation of retired nodes
        clock.advance(Duration.ofHours(30)); // Retire period spent
        MockDeployer deployer =
            new MockDeployer(provisioner,
                             clock,
                             Collections.singletonMap(applicationId, new MockDeployer.ApplicationContext(applicationId,
                                                                                                         cluster,
                                                                                                         Capacity.from(new ClusterResources(wantedNodes, 1, nodeResources)))));
        createRetiredExpirer(deployer).run();
        assertEquals(3, nodeRepository.getNodes(applicationId, Node.State.active).size());
        assertEquals(4, nodeRepository.getNodes(applicationId, Node.State.inactive).size());
        assertEquals(1, deployer.redeployments);

        // inactivated nodes are not retired
        for (Node node : nodeRepository.getNodes(applicationId, Node.State.inactive))
            assertFalse(node.allocation().get().membership().retired());
    }

    @Test
    public void ensure_early_inactivation() throws OrchestrationException {
        tester.makeReadyNodes(7, nodeResources);
        tester.makeReadyHosts(4, hostResources);

        ApplicationId applicationId = ApplicationId.from(TenantName.from("foo"), ApplicationName.from("bar"), InstanceName.from("fuz"));

        // Allocate content cluster of sizes 7 -> 2 -> 3:
        // Should end up with 3 nodes in the cluster (one previously retired), and 4 retired
        ClusterSpec cluster = ClusterSpec.request(ClusterSpec.Type.content, ClusterSpec.Id.from("test")).vespaVersion("6.42").build();
        int wantedNodes;
        activate(applicationId, cluster, wantedNodes=7, 1);
        activate(applicationId, cluster, wantedNodes=2, 1);
        activate(applicationId, cluster, wantedNodes=3, 1);
        assertEquals(7, nodeRepository.getNodes(applicationId, Node.State.active).size());
        assertEquals(0, nodeRepository.getNodes(applicationId, Node.State.inactive).size());

        // Cause inactivation of retired nodes
        MockDeployer deployer =
                new MockDeployer(provisioner,
                                 clock,
                                 Collections.singletonMap(
                                     applicationId,
                                     new MockDeployer.ApplicationContext(applicationId,
                                                                         cluster,
                                                                         Capacity.from(new ClusterResources(wantedNodes, 1, nodeResources)))));

        // Allow the 1st and 3rd retired nodes permission to inactivate
        doNothing()
                .doThrow(new OrchestrationException("Permission not granted 1"))
                .doNothing()
                .doThrow(new OrchestrationException("Permission not granted 2"))
                .when(orchestrator).acquirePermissionToRemove(any());

        RetiredExpirer retiredExpirer = createRetiredExpirer(deployer);
        retiredExpirer.run();
        assertEquals(5, nodeRepository.getNodes(applicationId, Node.State.active).size());
        assertEquals(2, nodeRepository.getNodes(applicationId, Node.State.inactive).size());
        assertEquals(1, deployer.redeployments);
        verify(orchestrator, times(4)).acquirePermissionToRemove(any());

        // Running it again has no effect
        retiredExpirer.run();
        assertEquals(5, nodeRepository.getNodes(applicationId, Node.State.active).size());
        assertEquals(2, nodeRepository.getNodes(applicationId, Node.State.inactive).size());
        assertEquals(1, deployer.redeployments);
        verify(orchestrator, times(6)).acquirePermissionToRemove(any());

        clock.advance(RETIRED_EXPIRATION.plusMinutes(1));
        retiredExpirer.run();
        assertEquals(3, nodeRepository.getNodes(applicationId, Node.State.active).size());
        assertEquals(4, nodeRepository.getNodes(applicationId, Node.State.inactive).size());
        assertEquals(2, deployer.redeployments);
        verify(orchestrator, times(6)).acquirePermissionToRemove(any());

        // inactivated nodes are not retired
        for (Node node : nodeRepository.getNodes(applicationId, Node.State.inactive))
            assertFalse(node.allocation().get().membership().retired());
    }

    private void activate(ApplicationId applicationId, ClusterSpec cluster, int nodes, int groups) {
        Capacity capacity = Capacity.from(new ClusterResources(nodes, groups, nodeResources));
        tester.activate(applicationId, tester.prepare(applicationId, cluster, capacity));
    }

    private RetiredExpirer createRetiredExpirer(Deployer deployer) {
        return new RetiredExpirer(nodeRepository,
                                  orchestrator,
                                  deployer,
                                  new TestMetric(),
                                  Duration.ofDays(30), /* Maintenance interval, use large value so it never runs by itself */
                                  RETIRED_EXPIRATION);
    }

}
