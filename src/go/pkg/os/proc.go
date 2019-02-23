package os

import (
	"bytes"
	"os/exec"
	"strings"

	log "github.com/sirupsen/logrus"
)

type Proc interface {
	Run(command string, args ...string) ([]string, error)
	IsRunning(processName string) (bool, error)
}

func NewProc() Proc {
	return &proc{}
}

type proc struct {
	Proc
}

func (*proc) Run(command string, args ...string) ([]string, error) {
	log.Debugf("Running command '%s' with arguments '%s'", command, args)
	cmd := exec.Command(command, args...)
	var out bytes.Buffer
	cmd.Stdout = &out
	err := cmd.Run()
	if err != nil {
		return nil, err
	}
	return strings.Split(out.String(), "\n"), nil
}

func (proc *proc) IsRunning(processName string) (bool, error) {
	lines, err := proc.Run("ps", "--no-headers", "-C", processName)
	if err != nil {
		return false, err
	}
	return len(lines) > 0, nil
}
