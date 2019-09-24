package cmd

import (
	"fmt"
	"path/filepath"
	"vectorized/pkg/checkers"
	"vectorized/pkg/cli"
	"vectorized/pkg/os"
	"vectorized/pkg/redpanda"
	"vectorized/pkg/tuners/factory"
	"vectorized/pkg/tuners/hwloc"
	"vectorized/pkg/utils"

	log "github.com/sirupsen/logrus"
	"github.com/spf13/afero"
	"github.com/spf13/cobra"
)

type prestartConfig struct {
	tuneEnabled  bool
	checkEnabled bool
}

func NewStartCommand(fs afero.Fs) *cobra.Command {
	prestartCfg := prestartConfig{}
	var (
		memoryFlag         string
		lockMemoryFlag     bool
		cpuSetFlag         string
		installDirFlag     string
		configFilePathFlag string
	)
	command := &cobra.Command{
		Use:   "start",
		Short: "Start redpanda",
		RunE: func(ccmd *cobra.Command, args []string) error {
			configFile, err := cli.GetOrFindConfig(fs, configFilePathFlag)
			if err != nil {
				return err
			}
			config, err := redpanda.ReadConfigFromPath(fs, configFile)
			if err != nil {
				return err
			}
			installDirectory, err := cli.GetOrFindInstallDir(fs, installDirFlag)
			if err != nil {
				return err
			}
			ioConfigFile := redpanda.GetIOConfigPath(filepath.Dir(configFile))
			if !utils.FileExists(fs, ioConfigFile) {
				ioConfigFile = ""
			}
			rpArgs := &redpanda.RedpandaArgs{
				ConfigFilePath: configFile,
				SeastarFlags: map[string]string{
					"io-properties-file": ioConfigFile,
					"lock-memory":        "true",
				},
			}
			err = prestart(fs, rpArgs, config, prestartCfg)
			if err != nil {
				return err
			}
			// Override all the defaults when flags are explicitly set
			if ccmd.Flags().Changed("memory") {
				rpArgs.SeastarFlags["memory"] = memoryFlag
			}
			if ccmd.Flags().Changed("cpuset") {
				rpArgs.SeastarFlags["cpuset"] = cpuSetFlag
			}
			if ccmd.Flags().Changed("lock-memory") {
				rpArgs.SeastarFlags["lock-memory"] = fmt.Sprint(lockMemoryFlag)
			}

			launcher := redpanda.NewLauncher(installDirectory, rpArgs)
			log.Info("Starting redpanda...")
			return launcher.Start()
		},
	}
	command.Flags().StringVar(&configFilePathFlag,
		"redpanda-cfg", "",
		" Redpanda config file, if not set the file will be searched for"+
			"in default locations")
	command.Flags().StringVar(&memoryFlag,
		"memory", "", "Amount of memory for redpanda to use, "+
			"if not specified redpanda will use all available memory")
	command.Flags().BoolVar(&lockMemoryFlag,
		"lock-memory", true, "If set, will prevent redpanda from swapping")
	command.Flags().StringVar(&cpuSetFlag, "cpuset", "",
		"Set of CPUs for redpanda to use in cpuset(7) format, "+
			"if not specified redpanda will use all available CPUs")
	command.Flags().StringVar(&installDirFlag,
		"install-dir", "",
		"Directory where redpanda has been installed")
	command.Flags().BoolVar(&prestartCfg.tuneEnabled, "tune", false,
		"When present will enable tuning before starting redpanda")
	command.Flags().BoolVar(&prestartCfg.checkEnabled, "check", true,
		"When set to false will disable system checking before starting redpanda")
	return command
}

func prestart(
	fs afero.Fs,
	args *redpanda.RedpandaArgs,
	config *redpanda.Config,
	prestartCfg prestartConfig,
) error {
	if prestartCfg.tuneEnabled {
		err := tuneAll(fs, args.SeastarFlags["cpuset"], config)
		if err != nil {
			return err
		}
		log.Info("System tune - PASSED")
	}
	if prestartCfg.checkEnabled {
		checkersMap, err := redpanda.RedpandaCheckers(fs,
			args.SeastarFlags["io-properties-file"], config)
		if err != nil {
			return err
		}
		err = check(checkersMap, checkFailedActions(args))
		if err != nil {
			return err
		}
		log.Info("System check - PASSED")
	}
	return nil
}

func tuneAll(fs afero.Fs, cpuSet string, config *redpanda.Config) error {
	params := &factory.TunerParams{}
	tunerFactory := factory.NewDirectExecutorTunersFactory(fs)
	hw := hwloc.NewHwLocCmd(os.NewProc())
	if cpuSet == "" {
		cpuMask, err := hw.All()
		if err != nil {
			return err
		}
		params.CpuMask = cpuMask
	} else {
		cpuMask, err := hwloc.TranslateToHwLocCpuSet(cpuSet)
		if err != nil {
			return err
		}
		params.CpuMask = cpuMask
	}

	err := factory.FillTunerParamsWithValuesFromConfig(params, config)
	if err != nil {
		return err
	}

	for _, tunerName := range factory.AvailableTuners() {
		tuner := tunerFactory.CreateTuner(tunerName, params)
		if supported, reason := tuner.CheckIfSupported(); supported == true {
			log.Debugf("Tuner paramters %+v", params)
			result := tuner.Tune()
			if result.IsFailed() {
				return result.GetError()
			}
		} else {
			log.Debugf("Tuner '%s' is not supported - %s", tunerName, reason)
		}
	}
	return nil
}

type checkFailedAction func(*checkers.CheckResult)

func checkFailedActions(
	args *redpanda.RedpandaArgs,
) map[redpanda.CheckerID]checkFailedAction {
	return map[redpanda.CheckerID]checkFailedAction{
		redpanda.SwapChecker: func(*checkers.CheckResult) {
			// Do not set --lock-memory flag when swap is disabled
			args.SeastarFlags["lock-memory"] = "false"
		},
	}
}

func check(
	checkersMap map[redpanda.CheckerID][]checkers.Checker,
	checkFailedActions map[redpanda.CheckerID]checkFailedAction,
) error {
	for checkerID, checkersSlice := range checkersMap {
		for _, checker := range checkersSlice {
			result := checker.Check()
			if result.Err != nil {
				return result.Err
			}
			if !result.IsOk {
				if action, exists := checkFailedActions[checkerID]; exists {
					action(result)
				}
				msg := fmt.Sprintf("System check '%s' failed. Required: %v, Current %v",
					checker.GetDesc(), checker.GetRequiredAsString(), result.Current)
				if checker.GetSeverity() == checkers.Fatal {
					return fmt.Errorf(msg)
				}
				log.Warn(msg)
			}
		}
	}
	return nil
}
