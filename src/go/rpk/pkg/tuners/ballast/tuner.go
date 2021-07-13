// Copyright 2021 Vectorized, Inc.
//
// Use of this software is governed by the Business Source License
// included in the file licenses/BSL.md
//
// As of the Change Date specified in that file, in accordance with
// the Business Source License, use of this software will be governed
// by the Apache License, Version 2.0

package ballast

import (
	"fmt"
	"os"
	"path/filepath"

	"github.com/pkg/errors"

	"github.com/docker/go-units"
	"github.com/vectorizedio/redpanda/src/go/rpk/pkg/config"
	"github.com/vectorizedio/redpanda/src/go/rpk/pkg/tuners"
	"github.com/vectorizedio/redpanda/src/go/rpk/pkg/tuners/executors"
	"github.com/vectorizedio/redpanda/src/go/rpk/pkg/tuners/executors/commands"
	"golang.org/x/sys/unix"
)

type ballastTuner struct {
	conf     config.Config
	executor executors.Executor
}

func NewBallastFileTuner(
	conf config.Config, executor executors.Executor,
) tuners.Tunable {
	return &ballastTuner{conf, executor}
}

func (t *ballastTuner) Tune() tuners.TuneResult {
	abspath, err := filepath.Abs(t.conf.Rpk.BallastFilePath)
	if err != nil {
		return tuners.NewTuneError(fmt.Errorf(
			"couldn't resolve the absolute file path for %s: %w",
			abspath,
			err,
		))
	}
	sizeBytes, err := units.FromHumanSize(t.conf.Rpk.BallastFileSize)
	if err != nil {
		return tuners.NewTuneError(fmt.Errorf(
			"'%s' is not a valid size unit.",
			t.conf.Rpk.BallastFileSize,
		))
	}
	cmd := commands.NewWriteSizedFileCmd(abspath, sizeBytes)
	err = cmd.Execute()
	if err != nil {
		return tuners.NewTuneError(err)
	}
	return tuners.NewTuneResult(false)
}

func (*ballastTuner) CheckIfSupported() (supported bool, reason string) {
	return true, ""
}

func executeBallast(path string, size int64) error {
	_, err := os.Stat(path)
	if err == nil {
		// If the file exists, error out to prevent overwriting important files
		// or filesystems.
		return fmt.Errorf("file '%s' already exists. If you're sure you want"+
			" to replace it, remove it first and then run this command again.",
			path,
		)
	}
	if !os.IsNotExist(err) {
		return err
	}

	f, err := os.OpenFile(path, os.O_WRONLY|os.O_CREATE, 0666)
	if err != nil {
		return err
	}
	defer f.Close()

	if err := unix.Fallocate(int(f.Fd()), 0, 0, size); err != nil {
		return fmt.Errorf("couldn't allocate the requested size while"+
			" creating the ballast file at '%s': %w",
			path,
			err,
		)
	}
	return errors.Wrap(f.Sync(), "couldn't sync the ballast file at "+path)
}
