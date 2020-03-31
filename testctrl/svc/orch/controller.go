// Copyright 2020 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package orch

import (
	"fmt"
	"strings"
	"sync"
	"time"

	"k8s.io/api/core/v1"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/client-go/kubernetes"
	"k8s.io/client-go/util/workqueue"

	"github.com/golang/glog"

	"github.com/grpc/grpc/testctrl/svc/types"
)

// executorCount specifies the maximum number of sessions that should be processed concurrently.
const executorCount = 1

// Controller manages active and idle sessions and their interactions with the Kubernetes API.
type Controller struct {
	clientset   *kubernetes.Clientset
	queue       workqueue.Interface
	monitors    map[string]*Monitor
	mux         sync.Mutex
	wg          sync.WaitGroup
	quitWatcher chan struct{}
}

// NewController constructs a Controller instance with a Kubernetes Clientset. This allows the
// controller to communicate with the Kubernetes API.
func NewController(clientset *kubernetes.Clientset) *Controller {
	c := &Controller{
		clientset: clientset,
		queue:     workqueue.New(),
		monitors:  make(map[string]*Monitor),
	}
	return c
}

// Schedule adds a session to the controller's queue. It will remain in the queue until there are
// sufficient resources for processing and monitoring. An error is returned if there are problems
// scheduling the session, such as invalid configurations.
func (c *Controller) Schedule(s *types.Session) error {
	// TODO(codeblooded): Add redundant validation checks
	c.queue.Add(s)
	return nil
}

// Start spawns goroutines to monitor the Kubernetes cluster for updates and to process a limited
// number of sessions at a time. An error is returned if there are problems within the goroutines,
// such as the inability to connect to the Kubernetes API.
func (c *Controller) Start() error {
	if err := c.startWatcher(); err != nil {
		return fmt.Errorf("controller start failed when starting watcher: %v", err)
	}

	c.startExecutors()
	return nil
}

// Stop attempts to terminate all orchestration goroutines spawned by a call to Start. It waits for
// executors to exit. Then, it kills the kubernetes watcher.
//
// If the timeout is reached before executors exit, an error is returned. The kubernetes watcher is
// still terminated. Any sessions running on the unterminated executors will likely fail.
func (c *Controller) Stop(timeout time.Duration) error {
	var err error

	c.queue.ShutDown()

	executorsDone := make(chan struct{})
	go func() {
		c.wg.Wait()
		close(executorsDone)
	}()

	select {
	case <-executorsDone:
		glog.Infof("controller: executors safely exited")
	case <-time.After(timeout):
		glog.Warning("controller: unable to wait for executors to safely exit, timed out")
		err = fmt.Errorf("executors did not safely exit before timeout")
	}

	close(c.quitWatcher)
	return err
}

// startWatcher creates a goroutine which watches for all kubernetes pod events in the cluster.
func (c *Controller) startWatcher() error {
	if c.clientset == nil {
		return fmt.Errorf("cannot start workers without Kubernetes clientset")
	}

	listOpts := metav1.ListOptions{
		Watch: true,
	}
	watcher, err := c.clientset.CoreV1().Pods(v1.NamespaceDefault).Watch(listOpts)
	if err != nil {
		return fmt.Errorf("could not start a pod watcher with list options %v: %v", listOpts, err)
	}

	watcherChan := watcher.ResultChan()
	c.quitWatcher = make(chan struct{})

	go func() {
		glog.Info("watcher: listening for kubernetes pod events")

		for {
			select {
			case event := <-watcherChan:
				pod := event.Object.(*v1.Pod)
				sessionName := pod.Labels["session-name"]

				c.mux.Lock()
				if monitor := c.monitors[sessionName]; monitor != nil {
					monitor.Update(pod)
				} else {
					glog.Warningf("watcher: found pods for session %v, but it has no active monitor", sessionName)
				}
				c.mux.Unlock()
			case <-c.quitWatcher:
				glog.Info("watcher: terminating gracefully")
				watcher.Stop()
				return
			}
		}
	}()

	return nil
}

// startExecutors create a set of goroutines. Each goroutine becomes responsible for a single
// session at a time.
func (c *Controller) startExecutors() {
	for i := 0; i < executorCount; i++ {
		info := &executorInfo{index: i}
		glog.Infof("controller: creating and starting executor[%v]", info.index)

		go func() {
			c.wg.Add(1)

			for {
				// start with clean state
				info.session = nil
				info.monitor = nil

				glog.Infof("executor[%v]: waiting for a session", info.index)
				si, quit := c.queue.Get()
				if quit {
					glog.Infof("executor[%v]: terminating gracefully", info.index)
					c.wg.Done()
					return
				}

				session := si.(*types.Session)
				monitor := NewMonitor()
				c.mux.Lock()
				c.monitors[session.Name] = monitor
				c.mux.Unlock()

				glog.Infof("executor[%v]: starting work on session %v", info.index, session.Name)
				info.session = session
				info.monitor = monitor
				if err := c.execute(info); err != nil {
					glog.Infof("executor[%v]: session %v terminated: %v", info.index, info.session.Name, err)
				}
				c.queue.Done(session)
				glog.Infof("executor[%v]: finished work on session %v", info.index, session.Name)
			}
		}()
	}
}

// executorInfo contains information that is used by an executor goroutine.
type executorInfo struct {
	index   int
	session *types.Session
	monitor *Monitor
}

// execute performs the provision, monitoring and teardown of a session's resources.
func (c *Controller) execute(info *executorInfo) error {
	if err := c.provision(info); err != nil {
		return fmt.Errorf("failed to provision resources: %v", err)
	}

	if err := c.monitorRun(info); err != nil {
		return fmt.Errorf("failed to finish testing: %v", err)
	}

	if err := c.teardown(info); err != nil {
		glog.Errorf("failed to teardown components: %v", err)
	}

	return nil
}

// deploy creates all kubernetes resources for a component by submitting a spec.
func (c *Controller) deploy(info *executorInfo, co *types.Component) error {
	kind := strings.ToLower(co.Kind.String())

	glog.V(2).Infof("executor[%v]: deploying %v component %v", info.index, kind, co.Name)

	pod := NewSpecBuilder(info.session, co).Pod()
	if _, err := c.clientset.CoreV1().Pods(v1.NamespaceDefault).Create(pod); err != nil {
		return fmt.Errorf("unable to deploy %v component %v: %v", kind, co.Name, err)
	}
	return nil
}

// provision creates kubernetes objects for every component, ensuring that they are healthy or
// returning an error.
func (c *Controller) provision(info *executorInfo) error {
	drivers := NewObjects(info.session.Driver)
	if count := len(drivers); count != 1 {
		return fmt.Errorf("expected exactly 1 driver, but got %v drivers", count)
	}
	driver := drivers[0]

	servers := NewObjects(info.session.ServerWorkers()...)
	if count := len(servers); count != 1 {
		return fmt.Errorf("expected exactly 1 server, but got %v servers", count)
	}
	server := servers[0]

	clients := NewObjects(info.session.ClientWorkers()...)

	workers := []*Object{server}
	workers = append(workers, clients...)
	var workerIPs []string

	for _, worker := range workers {
		info.monitor.Add(worker)

		if err := c.deploy(info, worker.Component()); err != nil {
			return err
		}

		var assignedIP bool

		for {

			if worker.Health() == Failed {
				return fmt.Errorf("component %v terminated due to unhealthy status: %v", worker.Name(), worker.Error())
			}

			if info.monitor.AnyFailed() {
				return fmt.Errorf("provision cancelled due to failure in component %v: %v",
					info.monitor.ErrObject().Name(), info.monitor.Error())
			}

			if !assignedIP {
				if ip := worker.PodStatus().PodIP; len(ip) > 0 {
					assignedIP = true
					workerIPs = append(workerIPs, ip)
					glog.V(2).Infof("executor[%v]: component %v was assigned IP address %v",
						info.index, worker.Name(), ip)
				}
			}

			if assignedIP && worker.Health() == Ready {
				glog.V(1).Infof("executor[%v]: component %v was successfully provisioned and is ready",
					info.index, worker.Name())
				break
			}
		}
	}

	info.monitor.Add(driver)
	for i, ip := range workerIPs {
		workerIPs[i] = ip + ":10000"
	}
	qpsWorkers := strings.Join(workerIPs, ",")
	driver.Component().Env["QPS_WORKERS"] = qpsWorkers

	if err := c.deploy(info, driver.Component()); err != nil {
		return fmt.Errorf("driver component %v could not be deployed: %v", driver.Name(), err)
	}

	for {
		if driver.Health() == Failed {
			return fmt.Errorf("driver component %v terminated due to unhealthy status: %v", driver.Name(), driver.Error())
		}

		if info.monitor.AnyFailed() {
			return fmt.Errorf("provision cancelled due to failure in component %v: %v",
				info.monitor.ErrObject().Name(), info.monitor.Error())
		}

		if driver.Health() == Ready {
			glog.V(1).Infof("component %v was successfully provisioned and is ready", driver.Name())
			break
		}
	}

	return nil
}

// monitorRun watches for an unhealthy status until all component kubernetes objects have finished
// gracefully. If it encounters an unhealthy status, it immediately returns an error. If components
// finish gracefully, it returns nil.
func (c *Controller) monitorRun(info *executorInfo) error {
	glog.Infof("executor[%v]: monitoring components while session %v runs",
		info.index, info.session.Name)

	for {
		if info.monitor.AnyFailed() {
			return fmt.Errorf("terminating because component %v has failed: %v",
				info.monitor.ErrObject().Name(), info.monitor.Error())
		}

		if info.monitor.AnySucceeded() {
			return nil
		}
	}
}

// teardown deletes all component kubernetes objects for the active session. It proxies any error
// from kubernetes in the return value.
func (c *Controller) teardown(info *executorInfo) error {
	listOpts := metav1.ListOptions{
		LabelSelector: fmt.Sprintf("session-name=%v", info.session.Name),
	}

	driverName := info.session.Driver.Name
	req := c.clientset.CoreV1().Pods(v1.NamespaceDefault).GetLogs(driverName, &v1.PodLogOptions{})
	logBytes, err := req.DoRaw()
	if err == nil {
		glog.Infof("executor[%v]: session %v had the following logs: %v",
			info.index, info.session.Name, string(logBytes))
	} else {
		glog.Infof("executor[%v]: session %v logs are inaccessible: %v",
			info.index, info.session.Name, err)
	}

	err = c.clientset.CoreV1().Pods(v1.NamespaceDefault).DeleteCollection(&metav1.DeleteOptions{}, listOpts)
	if err != nil {
		return fmt.Errorf("unable to delete components: %v", err)
	}

	return nil
}
