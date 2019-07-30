package redpanda

import (
	"errors"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"

	log "github.com/sirupsen/logrus"
	"golang.org/x/sys/unix"
)

type Launcher interface {
	Start() error
}

type RedpandaArgs struct {
	ConfigFilePath string
	CpuSet         string
	Memory         string
	IoConfigFile   string
}

func NewLauncher(
	installDir string, prestartAction func() error, args *RedpandaArgs,
) Launcher {
	return &launcher{
		installDir:     installDir,
		prestartAction: prestartAction,
		args:           args,
	}
}

type launcher struct {
	prestartAction func() error
	installDir     string
	args           *RedpandaArgs
}

func (l *launcher) Start() error {
	err := l.prestartAction()
	if err != nil {
		return err
	}
	binary, err := l.getBinary()
	if err != nil {
		return err
	}

	if l.args.ConfigFilePath == "" {
		return errors.New("Redpanda config file is required")
	}

	redpandaArgs := []string{
		"redpanda",
		"--redpanda-cfg",
		l.args.ConfigFilePath,
	}

	if l.args.CpuSet != "" {
		redpandaArgs = append(redpandaArgs, "--cpuset", l.args.CpuSet)
	}
	if l.args.Memory != "" {
		redpandaArgs = append(redpandaArgs, "--memory", l.args.Memory)
	}
	if l.args.IoConfigFile != "" {
		redpandaArgs = append(redpandaArgs, "--io-properties-file",
			l.args.IoConfigFile)
	}

	log.Debugf("Starting '%s' with arguments '%v'", binary, redpandaArgs)

	var rpEnv []string
	ldLibraryPathPattern := regexp.MustCompile("^LD_LIBRARY_PATH=.*$")
	for _, ev := range os.Environ() {
		if !ldLibraryPathPattern.MatchString(ev) {
			rpEnv = append(rpEnv, ev)
		}
	}
	return unix.Exec(binary, redpandaArgs, rpEnv)
}

func (l *launcher) getBinary() (string, error) {
	path, err := exec.LookPath(filepath.Join(l.installDir, "bin", "redpanda"))
	if err != nil {
		return "", err
	}
	return path, nil
}
