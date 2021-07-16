// Copyright 2021 Vectorized, Inc.
//
// Use of this software is governed by the Business Source License
// included in the file licenses/BSL.md
//
// As of the Change Date specified in that file, in accordance with
// the Business Source License, use of this software will be governed
// by the Apache License, Version 2.0

package resources

import (
	"context"
	"fmt"
	"strconv"

	"github.com/go-logr/logr"
	redpandav1alpha1 "github.com/vectorizedio/redpanda/src/go/k8s/apis/redpanda/v1alpha1"
	"github.com/vectorizedio/redpanda/src/go/k8s/pkg/labels"
	corev1 "k8s.io/api/core/v1"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/apimachinery/pkg/runtime"
	"k8s.io/apimachinery/pkg/types"
	"k8s.io/apimachinery/pkg/util/intstr"
	k8sclient "sigs.k8s.io/controller-runtime/pkg/client"
	"sigs.k8s.io/controller-runtime/pkg/controller/controllerutil"
)

var _ Resource = &NodePortServiceResource{}

// NodePortServiceResource is part of the reconciliation of redpanda.vectorized.io CRD
// that assigns port on each node to enable external connectivity
type NodePortServiceResource struct {
	k8sclient.Client
	scheme       *runtime.Scheme
	pandaCluster *redpandav1alpha1.Cluster
	broker       int32
	svcPorts     []NamedServicePort
	logger       logr.Logger
}

// NewNodePortService creates NodePortServiceResource
func NewNodePortService(
	client k8sclient.Client,
	pandaCluster *redpandav1alpha1.Cluster,
	scheme *runtime.Scheme,
	broker int32,
	svcPorts []NamedServicePort,
	logger logr.Logger,
) *NodePortServiceResource {
	return &NodePortServiceResource{
		client,
		scheme,
		pandaCluster,
		broker,
		svcPorts,
		logger.WithValues("Kind", serviceKind(), "ServiceType", "NodePort"),
	}
}

// Ensure will manage kubernetes v1.Service for redpanda.vectorized.io custom resource
func (r *NodePortServiceResource) Ensure(ctx context.Context) error {
	if r.pandaCluster.ExternalListener() == nil {
		return nil
	}

	obj, err := r.obj()
	if err != nil {
		return fmt.Errorf("unable to construct object: %w", err)
	}
	created, err := CreateIfNotExists(ctx, r, obj, r.logger)
	if err != nil || created {
		return err
	}
	var svc corev1.Service
	name := r.Key()
	if r.broker >= 0 {
		name = r.brokerKey(r.broker)
	}
	err = r.Get(ctx, name, &svc)
	if err != nil {
		return fmt.Errorf("error while fetching Service resource: %w", err)
	}

	copyPorts(obj.(*corev1.Service), &svc)
	return Update(ctx, &svc, obj, r.Client, r.logger)
}

func copyPorts(newSvc, currentSvc *corev1.Service) {
	for i := range currentSvc.Spec.Ports {
		for j := range newSvc.Spec.Ports {
			if newSvc.Spec.Ports[j].Port == currentSvc.Spec.Ports[i].Port {
				newSvc.Spec.Ports[j].NodePort = currentSvc.Spec.Ports[i].NodePort
				break
			}
		}
	}
}

// obj returns resource managed client.Object
func (r *NodePortServiceResource) obj() (k8sclient.Object, error) {
	ports := make([]corev1.ServicePort, 0, len(r.svcPorts))
	for _, svcPort := range r.svcPorts {
		port := corev1.ServicePort{
			Name:       svcPort.Name,
			Protocol:   corev1.ProtocolTCP,
			Port:       int32(svcPort.Port),
			TargetPort: intstr.FromInt(svcPort.Port),
		}
		if svcPort.NodePort > 0 {
			port.NodePort = int32(svcPort.NodePort)
		}
		ports = append(ports, port)
	}

	objLabels := labels.ForCluster(r.pandaCluster)
	selectorLabels := objLabels.AsAPISelector().MatchLabels
	name := r.Key()
	if r.broker >= 0 {
		name = r.brokerKey(r.broker)
		selectorLabels["statefulset.kubernetes.io/pod-name"] = name.Name
	}
	svc := &corev1.Service{
		ObjectMeta: metav1.ObjectMeta{
			Namespace: name.Namespace,
			Name:      name.Name,
			Labels:    objLabels,
		},
		TypeMeta: metav1.TypeMeta{
			Kind:       "Service",
			APIVersion: "v1",
		},
		Spec: corev1.ServiceSpec{
			// The service type node port assigned port to each node in the cluster.
			// This gives a way for operator to assign unused port to the redpanda cluster.
			// Reference:
			// https://kubernetes.io/docs/tutorials/services/source-ip/#source-ip-for-services-with-type-nodeport
			Type: corev1.ServiceTypeNodePort,
			// If you set service.spec.externalTrafficPolicy to the value Local,
			// kube-proxy only proxies proxy requests to local endpoints,
			// and does not forward traffic to other nodes.
			// Reference:
			// https://kubernetes.io/docs/tasks/access-application-cluster/create-external-load-balancer/#preserving-the-client-source-ip
			// https://blog.getambassador.io/externaltrafficpolicy-local-on-kubernetes-e66e498212f9
			ExternalTrafficPolicy: corev1.ServiceExternalTrafficPolicyTypeLocal,
			Ports:                 ports,
			Selector:              selectorLabels,
		},
	}

	err := controllerutil.SetControllerReference(r.pandaCluster, svc, r.scheme)
	if err != nil {
		return nil, err
	}

	return svc, nil
}

// Key returns namespace/name object that is used to identify object.
// For reference please visit types.NamespacedName docs in k8s.io/apimachinery
func (r *NodePortServiceResource) Key() types.NamespacedName {
	return types.NamespacedName{Name: r.pandaCluster.Name + "-external", Namespace: r.pandaCluster.Namespace}
}

func (r *NodePortServiceResource) brokerKey(broker int32) types.NamespacedName {
	suffix := "-" + strconv.Itoa(int(broker-1))
	return types.NamespacedName{Name: r.pandaCluster.Name + suffix, Namespace: r.pandaCluster.Namespace}
}
