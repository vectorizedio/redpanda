// Copyright 2021 Vectorized, Inc.
//
// Use of this software is governed by the Business Source License
// included in the file licenses/BSL.md
//
// As of the Change Date specified in that file, in accordance with
// the Business Source License, use of this software will be governed
// by the Apache License, Version 2.0

package commands

import (
	"bufio"
	"fmt"
	"os"

	"github.com/pkg/errors"

	log "github.com/sirupsen/logrus"
	"golang.org/x/sys/unix"
)

type writeSizedFileCommand struct {
	path      string
	sizeBytes int64
}

func NewWriteSizedFileCmd(path string, sizeBytes int64) Command {
	return &writeSizedFileCommand{path, sizeBytes}
}

func (c *writeSizedFileCommand) Execute() error {
	log.Debugf("Creating  '%s' (%d B)", c.path, c.sizeBytes)

	_, err := os.Stat(c.path)
	if err == nil {
		// If the file exists, error out to prevent overwriting important files
		// or filesystems.
		return errBallastExists(c.path)
	}
	if !os.IsNotExist(err) {
		return err
	}

	// the 'os' package needs to be used instead of 'afero', because the file
	// handles returned by afero don't have a way to get their file descriptor
	// (i.e. an Fd() method):
	// https://github.com/spf13/afero/issues/234
	f, err := os.OpenFile(c.path, os.O_WRONLY|os.O_CREATE, 0666)
	if err != nil {
		return err
	}
	defer f.Close()

	if err := unix.Fallocate(int(f.Fd()), 0, 0, c.sizeBytes); err != nil {
		return fmt.Errorf("couldn't allocate the requested size while"+
			" creating the ballast file at '%s': %w",
			c.path,
			err,
		)
	}
	return errors.Wrap(f.Sync(), "couldn't sync the ballast file at "+c.path)
}

func (c *writeSizedFileCommand) RenderScript(w *bufio.Writer) error {
	fmt.Fprintf(
		w,
		`if test -f "%s"; then
  echo "%v"
else
  fallocate -l %d %s
fi
`,
		c.path,
		errBallastExists(c.path),
		c.sizeBytes,
		c.path,
	)
	return w.Flush()
}

func errBallastExists(path string) error {
	return fmt.Errorf(
		"file '%s' already exists. If you're sure you want"+
			" to replace it, remove it first and then run this tuner again.",
		path,
	)
}
