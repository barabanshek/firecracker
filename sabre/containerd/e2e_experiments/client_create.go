package main

import (
	"context"
	"flag"
	"fmt"
	"net"

	// "fmt"

	// "sync"
	"math/rand"

	"bytes"
	"os/exec"
	"time"

	"github.com/containerd/containerd/namespaces"
	"github.com/vhive-serverless/vhive/snapshotting"

	log "github.com/sirupsen/logrus"
	ctriface "github.com/vhive-serverless/vhive/ctriface"
)

var (
	isUPFEnabled = flag.Bool("upf", false, "Set UPF enabled")
	isLazyMode   = flag.Bool("lazy", false, "Set lazy serving on or off")
	//nolint:deadcode,unused,varcheck
	isWithCache = flag.Bool("withCache", false, "Do not drop the cache before measurements")
)

const charset = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"

var seededRand *rand.Rand = rand.New(rand.NewSource(time.Now().UnixNano()))

func generateRandomString(length int) string {
	b := make([]byte, length)
	for i := range b {
		b[i] = charset[seededRand.Intn(len(charset))]
	}
	return string(b)
}

func main() {
	testImageNameFlag := flag.String("image", "image", "image")
	testInvocationCommandFlag := flag.String("invoke", "invoke", "invoke")
	testMemorySizeFlag := flag.Int("memsize", 256, "memory size in MB")
	testPauseFlag := flag.Int("pause", 0, "Pause in sec")
	flag.Parse()

	testImageName := *testImageNameFlag
	testInvocationCommand := *testInvocationCommandFlag

	namespaceName := "firecracker-containerd"
	testTimeout := 120 * time.Second
	ctx, cancel := context.WithTimeout(namespaces.WithNamespace(context.Background(), namespaceName), testTimeout)
	defer cancel()

	snapshotter := flag.String("ss", "devmapper", "snapshotter name")
	testModeOn := false

	orch := ctriface.NewOrchestrator(
		*snapshotter,
		"",
		ctriface.WithTestModeOn(testModeOn),
		ctriface.WithUPF(*isUPFEnabled),
		ctriface.WithLazyMode(*isLazyMode),
	)

	vmID := generateRandomString(10)

	// Setup.
	{
		sh_cmd := exec.Command("/bin/sh", "cleanup.sh")
		var outb, errb bytes.Buffer
		sh_cmd.Stdout = &outb
		sh_cmd.Stderr = &errb
		sh_err := sh_cmd.Run()
		if sh_err != nil {
			log.Fatalf("failed to exec bash")
		}
		log.Info("Setup done: ", outb.String())
	}

	rsp, metrics, err := orch.StartVM(ctx, vmID, testImageName, *testMemorySizeFlag)
	if err != nil {
		log.Fatalf("failed to StartVM %v", err)
	}
	log.Info("VM started, IP= %v", rsp.GuestIP)
	metrics.PrintAll()

	time.Sleep(time.Duration(*testPauseFlag) * time.Second)

	// Invoke.
	{
		log.Info("Invoke...")
		var sh_cmd *exec.Cmd = exec.Command("/bin/sh", "/home/nikita/vhive/experiments/invoke.sh", testInvocationCommand, "record")
		var outb, errb bytes.Buffer
		sh_cmd.Stdout = &outb
		sh_cmd.Stderr = &errb
		sh_err := sh_cmd.Run()
		if sh_err != nil {
			log.Fatalf("failed to exec bash")
		}
		fmt.Println("Invoked: ", outb.String())
	}

	// Wait and snapshot.
	time.Sleep(1 * time.Second)
	err = orch.PauseVM(ctx, vmID)
	if err != nil {
		log.Fatalf("failed to PauseVM %v", err)
	}
	log.Info("VM paused")

	revision := "myrev-4"
	snap := snapshotting.NewSnapshot(revision, "/fccd/snapshots", testImageName)
	err = snap.CreateSnapDir()
	if err != nil {
		log.Fatalf("failed to CreateSnapDir %v", err)
	}

	err = orch.CreateSnapshot(ctx, vmID, snap)
	if err != nil {
		log.Fatalf("failed to CreateSnapshot %v", err)
	}
	log.Info("VM snapshotted")

	_, err = orch.ResumeVM(ctx, vmID)
	if err != nil {
		log.Fatalf("failed to ResumeVM %v", err)
	}
	log.Info("VM resumed")

	// Terminate VM.
	err = orch.StopSingleVM(ctx, vmID)
	if err != nil {
		log.Fatalf("failed to Stop VM %v", err)
	}
	log.Info("VM stopped.")

	// Drop cache.
	{
		sh_cmd := exec.Command("/bin/sh", "drop_cache.sh")
		var outb, errb bytes.Buffer
		sh_cmd.Stdout = &outb
		sh_cmd.Stderr = &errb
		sh_err := sh_cmd.Run()
		if sh_err != nil {
			log.Fatalf("failed to exec bash")
		}
		log.Info("Dro cache done: ", outb.String())

		// Pre-warm the hypervisor with a hello, world image.
		vmID_prewarm := generateRandomString(10)
		_, _, err := orch.StartVM(ctx, vmID_prewarm, "localhost:5000/hello_world:latest", 256)
		if err != nil {
			log.Fatalf("failed to StartVM %v", err)
		}
		err = orch.StopSingleVM(ctx, vmID_prewarm)
		if err != nil {
			log.Fatalf("failed to Stop VM %v", err)
		}
		log.Info("Pre-warm VM stopped.")
	}

	// Wait and resume.
	time.Sleep(1 * time.Second)
	_, metrics, err = orch.LoadSnapshot(ctx, vmID, snap, *testMemorySizeFlag)
	if err != nil {
		log.Fatalf("failed to StartVM %v", err)
	}
	log.Info("VM resumed from the snapshot")
	metrics.PrintAll()

	//
	conn, err := net.Dial("unix", "/tmp/reap.sock")
	if err != nil {
		fmt.Println("Error dialing Reap recorder.")
		return
	}
	fmt.Fprintf(conn, "RECORD")

	// Invoke.
	{
		log.Info("Invoke...")
		var sh_cmd *exec.Cmd = exec.Command("/bin/sh", "/home/nikita/vhive/experiments/invoke.sh", testInvocationCommand, "record")
		var outb, errb bytes.Buffer
		sh_cmd.Stdout = &outb
		sh_cmd.Stderr = &errb
		sh_err := sh_cmd.Run()
		if sh_err != nil {
			log.Fatalf("failed to exec bash")
		}
		fmt.Println("Invoked: ", outb.String())
	}

	fmt.Fprintf(conn, "STOP_RECORD")
}
