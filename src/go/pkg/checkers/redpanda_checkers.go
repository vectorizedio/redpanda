package checkers

import (
	"vectorized/redpanda"
	"vectorized/system"

	"github.com/spf13/afero"
)

func NewConfigChecker(config *redpanda.Config) Checker {
	return NewEqualityChecker(
		"Config file valid",
		true,
		true,
		func() (interface{}, error) {
			return redpanda.CheckConfig(config), nil
		})
}

func NewDataDirWritableChecker(fs afero.Fs, path string) Checker {
	return NewEqualityChecker(
		"Data directory is writable",
		true,
		true,
		func() (interface{}, error) {
			return system.DirectoryIsWriteable(fs, path)
		})
}

func NewFreeDiskSpaceChecker(path string) Checker {
	return NewFloatChecker(
		"Data partition free space [GB]",
		false,
		func(current float64) bool {
			return current >= 10.0
		},
		func() string {
			return ">= 10"
		},
		func() (float64, error) {
			return system.GetFreeDiskSpaceGB(path)
		})
}

func NewMemoryChecker() Checker {
	return NewIntChecker(
		"Free memory [MB]",
		true,
		func(current int) bool {
			return current >= 2048
		},
		func() string {
			return "2048"
		},
		system.GetMemAvailableMB,
	)
}

func NewFilesystemTypeChecker(fs afero.Fs, path string) Checker {
	return NewEqualityChecker(
		"Data directory filesystem type",
		false,
		"xfs",
		func() (interface{}, error) {
			return system.GetFilesystemType(fs, path)
		})
}

func NewIOConfigFileExistanceChecker(
	fs afero.Fs, filePath string,
) Checker {
	return NewFileExistanceChecker(
		fs,
		"I/O config file present",
		false,
		filePath)
}
