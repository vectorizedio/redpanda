package config

import (
	"path/filepath"
	"testing"
	"vectorized/pkg/utils"
	vyaml "vectorized/pkg/yaml"

	"github.com/spf13/afero"
	"github.com/spf13/viper"
	"github.com/stretchr/testify/require"
	"gopkg.in/yaml.v2"
)

func getValidConfig() *Config {
	conf := DefaultConfig()
	conf.Redpanda.SeedServers = []*SeedServer{
		&SeedServer{
			SocketAddress{"127.0.0.1", 33145},
			1,
		},
		&SeedServer{
			SocketAddress{"127.0.0.1", 33146},
			2,
		},
	}
	conf.Rpk = &RpkConfig{
		EnableUsageStats:         true,
		TuneNetwork:              true,
		TuneDiskScheduler:        true,
		TuneNomerges:             true,
		TuneDiskIrq:              true,
		TuneFstrim:               true,
		TuneCpu:                  true,
		TuneAioEvents:            true,
		TuneClocksource:          true,
		TuneSwappiness:           true,
		TuneTransparentHugePages: true,
		EnableMemoryLocking:      true,
		TuneCoredump:             true,
		CoredumpDir:              "/var/lib/redpanda/coredumps",
		WellKnownIo:              "vendor:vm:storage",
	}
	return &conf
}

func TestSet(t *testing.T) {
	tests := []struct {
		name      string
		key       string
		value     string
		format    string
		expected  interface{}
		expectErr bool
	}{
		{
			name:     "it should parse '1' as an int and not as bool (true)",
			key:      "redpanda.node_id",
			value:    "1",
			format:   "single",
			expected: 1,
		},
		{
			name:     "it should set single integer fields",
			key:      "redpanda.node_id",
			value:    "54312",
			format:   "single",
			expected: 54312,
		},
		{
			name:     "it should set single float fields",
			key:      "redpanda.float_field",
			value:    "42.3",
			format:   "single",
			expected: 42.3,
		},
		{
			name:     "it should set single string fields",
			key:      "redpanda.data_directory",
			value:    "'/var/lib/differentdir'",
			format:   "single",
			expected: "'/var/lib/differentdir'",
		},
		{
			name:     "it should set single bool fields",
			key:      "rpk.enable_usage_stats",
			value:    "true",
			format:   "single",
			expected: true,
		},
		{
			name:   "it should partially set map fields (yaml)",
			key:    "rpk",
			value:  `tune_disk_irq: true`,
			format: "yaml",
			expected: map[string]interface{}{
				"enable_usage_stats": false,
				"tls": map[string]interface{}{
					"cert_file":       "",
					"key_file":        "",
					"truststore_file": "",
				},
				"tune_network":               false,
				"tune_disk_scheduler":        false,
				"tune_disk_nomerges":         false,
				"tune_disk_irq":              true,
				"tune_cpu":                   false,
				"tune_aio_events":            false,
				"tune_clocksource":           false,
				"tune_swappiness":            false,
				"tune_transparent_hugepages": false,
				"enable_memory_locking":      false,
				"tune_fstrim":                false,
				"tune_coredump":              false,
				"coredump_dir":               "/var/lib/redpanda/coredump",
			},
		},
		{
			name: "it should partially set map fields (json)",
			key:  "redpanda.kafka_api",
			value: `{
  "address": "192.168.54.2"
}`,
			format: "json",
			expected: map[string]interface{}{
				"address": "192.168.54.2",
				"port":    9092,
			},
		},
		{
			name:      "it should fail if the new value is invalid",
			key:       "redpanda",
			value:     `{"data_directory": ""}`,
			format:    "json",
			expectErr: true,
		},
		{
			name:      "it should fail if the value isn't well formatted (json)",
			key:       "redpanda",
			value:     `{"seed_servers": []`,
			format:    "json",
			expectErr: true,
		},
		{
			name: "it should fail if the value isn't well formatted (yaml)",
			key:  "redpanda",
			value: `seed_servers:
- host:
  address: "123.`,
			format:    "yaml",
			expectErr: true,
		},
		{
			name:      "it should fail if the format isn't supported",
			key:       "redpanda",
			value:     "node_id=1",
			format:    "toml",
			expectErr: true,
		},
		{
			name:      "it should fail if no key is passed",
			value:     `node_id=1`,
			expectErr: true,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			fs := afero.NewMemMapFs()
			conf := DefaultConfig()
			err := WriteConfig(fs, &conf, conf.ConfigFile)
			require.NoError(t, err)
			err = Set(fs, tt.key, tt.value, tt.format, conf.ConfigFile)
			if tt.expectErr {
				require.Error(t, err)
				return
			}
			require.NoError(t, err)
			v := viper.New()
			v.SetFs(fs)
			v.SetConfigType("yaml")
			v.SetConfigFile(conf.ConfigFile)
			err = v.ReadInConfig()
			require.NoError(t, err)
			val := v.Get(tt.key)
			require.Exactly(t, tt.expected, val)
		})
	}
}

func TestDefaultConfig(t *testing.T) {
	defaultConfig := DefaultConfig()
	expected := Config{
		ConfigFile: "/etc/redpanda/redpanda.yaml",
		Redpanda: &RedpandaConfig{
			Directory:   "/var/lib/redpanda/data",
			RPCServer:   SocketAddress{"0.0.0.0", 33145},
			KafkaApi:    SocketAddress{"0.0.0.0", 9092},
			AdminApi:    SocketAddress{"0.0.0.0", 9644},
			Id:          0,
			SeedServers: []*SeedServer{},
		},
		Rpk: &RpkConfig{
			CoredumpDir: "/var/lib/redpanda/coredump",
		},
	}
	require.Exactly(t, expected, defaultConfig)
}

func TestReadConfigFromPath(t *testing.T) {
	const baseDir string = "/etc/redpanda"
	tests := []struct {
		name    string
		path    string
		before  func(afero.Fs, string) error
		want    func() *Config
		wantErr bool
	}{
		{
			name: "shall return a config struct filled with values from the file",
			before: func(fs afero.Fs, path string) error {
				bs, err := yaml.Marshal(getValidConfig())
				if err != nil {
					return err
				}
				if err = fs.MkdirAll(baseDir, 0755); err != nil {
					return err
				}
				_, err = utils.WriteBytes(fs, bs, path)
				return err
			},
			path:    baseDir + "/redpanda.yaml",
			want:    getValidConfig,
			wantErr: false,
		},
		// TODO: Add tests for when the config file has missing objects
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			fs := afero.NewMemMapFs()
			err := tt.before(fs, tt.path)
			require.NoError(t, err)
			got, err := ReadConfigFromPath(fs, tt.path)
			want := tt.want()
			if tt.wantErr {
				require.Error(t, err)
				return
			}
			require.NoError(t, err)
			require.Exactly(t, want, got)
		})
	}
}

func TestWriteConfig(t *testing.T) {
	const path string = "/redpanda.yaml"
	type args struct {
	}
	tests := []struct {
		name         string
		existingConf string
		conf         func() *Config
		wantErr      bool
		expected     string
	}{
		{
			name: "shall write a valid config file without an rpk config object",
			conf: func() *Config {
				c := getValidConfig()
				c.Rpk = nil
				return c
			},
			wantErr: false,
			expected: `config_file: /etc/redpanda/redpanda.yaml
redpanda:
  admin:
    address: 0.0.0.0
    port: 9644
  data_directory: /var/lib/redpanda/data
  kafka_api:
    address: 0.0.0.0
    port: 9092
  kafka_api_tls:
    cert_file: ""
    enabled: false
    key_file: ""
    truststore_file: ""
  node_id: 0
  rpc_server:
    address: 0.0.0.0
    port: 33145
  seed_servers:
  - host:
      address: 127.0.0.1
      port: 33145
    node_id: 1
  - host:
      address: 127.0.0.1
      port: 33146
    node_id: 2
`,
		},
		{
			name: "shall fail with an invalid config",
			conf: func() *Config {
				c := getValidConfig()
				c.Redpanda.Directory = ""
				return c
			},
			wantErr: true,
		},
		{
			name: "shall fail if there's no redpanda config",
			conf: func() *Config {
				return &Config{}
			},
			wantErr: true,
		},
		{
			name:    "shall write a valid config file with an rpk config object",
			conf:    getValidConfig,
			wantErr: false,
			expected: `config_file: /etc/redpanda/redpanda.yaml
redpanda:
  admin:
    address: 0.0.0.0
    port: 9644
  data_directory: /var/lib/redpanda/data
  kafka_api:
    address: 0.0.0.0
    port: 9092
  kafka_api_tls:
    cert_file: ""
    enabled: false
    key_file: ""
    truststore_file: ""
  node_id: 0
  rpc_server:
    address: 0.0.0.0
    port: 33145
  seed_servers:
  - host:
      address: 127.0.0.1
      port: 33145
    node_id: 1
  - host:
      address: 127.0.0.1
      port: 33146
    node_id: 2
rpk:
  coredump_dir: /var/lib/redpanda/coredumps
  enable_memory_locking: true
  enable_usage_stats: true
  tls:
    cert_file: ""
    key_file: ""
    truststore_file: ""
  tune_aio_events: true
  tune_clocksource: true
  tune_coredump: true
  tune_cpu: true
  tune_disk_irq: true
  tune_disk_nomerges: true
  tune_disk_scheduler: true
  tune_fstrim: true
  tune_network: true
  tune_swappiness: true
  tune_transparent_hugepages: true
  well_known_io: vendor:vm:storage
`,
		},
		{
			name: "shall leave unrecognized fields untouched",
			existingConf: `config_file: /etc/redpanda/redpanda.yaml
redpanda:
  admin:
    address: 0.0.0.0
    port: 9644
  admin_api_doc_dir: /etc/redpanda/doc
  data_directory: /var/lib/redpanda/data
  default_window_sec: 100
  kafka_api:
    address: 0.0.0.0
    port: 9092
  node_id: 0
  rpc_server:
    address: 0.0.0.0
    port: 33145
  seed_servers:
  - host:
      address: 127.0.0.1
      port: 33145
    node_id: 1
  - host:
      address: 127.0.0.1
      port: 33146
    node_id: 2
  target_quota_byte_rate: 1000000
unrecognized_top_field:
  child: true
`,
			conf:    getValidConfig,
			wantErr: false,
			expected: `config_file: /etc/redpanda/redpanda.yaml
redpanda:
  admin:
    address: 0.0.0.0
    port: 9644
  admin_api_doc_dir: /etc/redpanda/doc
  data_directory: /var/lib/redpanda/data
  default_window_sec: 100
  kafka_api:
    address: 0.0.0.0
    port: 9092
  kafka_api_tls:
    cert_file: ""
    enabled: false
    key_file: ""
    truststore_file: ""
  node_id: 0
  rpc_server:
    address: 0.0.0.0
    port: 33145
  seed_servers:
  - host:
      address: 127.0.0.1
      port: 33145
    node_id: 1
  - host:
      address: 127.0.0.1
      port: 33146
    node_id: 2
  target_quota_byte_rate: 1000000
rpk:
  coredump_dir: /var/lib/redpanda/coredumps
  enable_memory_locking: true
  enable_usage_stats: true
  tls:
    cert_file: ""
    key_file: ""
    truststore_file: ""
  tune_aio_events: true
  tune_clocksource: true
  tune_coredump: true
  tune_cpu: true
  tune_disk_irq: true
  tune_disk_nomerges: true
  tune_disk_scheduler: true
  tune_fstrim: true
  tune_network: true
  tune_swappiness: true
  tune_transparent_hugepages: true
  well_known_io: vendor:vm:storage
unrecognized_top_field:
  child: true
`,
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			fs := afero.NewMemMapFs()
			if tt.existingConf != "" {
				_, err := utils.WriteBytes(fs, []byte(tt.existingConf), path)
				require.NoError(t, err)
			} else {
				// Write the config file so that WriteConfig backs it up
				// when the new one is written.
				err := vyaml.Persist(fs, tt.conf(), path)
				require.NoError(t, err)
			}
			err := WriteConfig(fs, tt.conf(), path)
			if tt.wantErr {
				require.Error(t, err)
				return
			}
			require.NoError(t, err)

			contentBytes, err := afero.ReadFile(fs, path)
			require.NoError(t, err)
			content := string(contentBytes)
			require.Equal(t, tt.expected, content)
			backup, err := findBackup(fs, filepath.Dir(path))
			require.NoError(t, err)
			_, err = fs.Stat(backup)
			require.NoError(t, err)
		})
	}
}

func TestInitConfig(t *testing.T) {
	tests := []struct {
		name        string
		setup       func(afero.Fs) error
		configFile  string
		expectError bool
	}{
		{
			name:       "it should generate a config file at the given location",
			configFile: DefaultConfig().ConfigFile,
		},
		{
			name: "it shouldn't fail if there's a config file already",
			setup: func(fs afero.Fs) error {
				conf := DefaultConfig()
				return WriteConfig(fs, &conf, conf.ConfigFile)
			},
			configFile: DefaultConfig().ConfigFile,
		},
		{
			name: "it should fail if the existing config file's content isn't valid yaml",
			setup: func(fs afero.Fs) error {
				bs := []byte(`redpanda:
- something`)
				return vyaml.Persist(fs, bs, DefaultConfig().ConfigFile)
			},
			configFile:  DefaultConfig().ConfigFile,
			expectError: true,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			fs := afero.NewMemMapFs()
			if tt.setup != nil {
				err := tt.setup(fs)
				require.NoError(t, err)
			}
			_, err := ReadOrGenerate(fs, tt.configFile)
			if tt.expectError {
				require.Error(t, err)
				return
			}
			require.NoError(t, err)
			_, err = ReadConfigFromPath(fs, tt.configFile)
			require.NoError(t, err)
		})
	}
}

func TestSetMode(t *testing.T) {
	fillRpkConfig := func(mode string) Config {
		conf := DefaultConfig()
		val := mode == ModeProd
		conf.Rpk = &RpkConfig{
			TuneNetwork:       val,
			TuneDiskScheduler: val,
			TuneNomerges:      val,
			TuneDiskIrq:       val,
			TuneFstrim:        val,
			TuneCpu:           val,
			TuneAioEvents:     val,
			TuneClocksource:   val,
			TuneSwappiness:    val,
			CoredumpDir:       conf.Rpk.CoredumpDir,
		}
		return conf
	}

	tests := []struct {
		name           string
		mode           string
		expectedConfig Config
		expectedErrMsg string
	}{
		{
			name:           "it should disable all tuners for dev mode",
			mode:           ModeDev,
			expectedConfig: fillRpkConfig(ModeDev),
		},
		{
			name:           "it should disable all tuners for dev mode ('development')",
			mode:           "development",
			expectedConfig: fillRpkConfig(ModeDev),
		},
		{
			name:           "it should disable all tuners for dev mode ('')",
			mode:           "",
			expectedConfig: fillRpkConfig(ModeDev),
		},
		{
			name:           "it should enable all the default tuners for prod mode",
			mode:           ModeProd,
			expectedConfig: fillRpkConfig(ModeProd),
		},
		{
			name:           "it should enable all the default tuners for prod mode ('production')",
			mode:           ModeProd,
			expectedConfig: fillRpkConfig(ModeProd),
		},
		{
			name:           "it should return an error for invalid modes",
			mode:           "winning",
			expectedErrMsg: "'winning' is not a supported mode. Available modes: dev, development, prod, production",
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(st *testing.T) {
			defaultConf := DefaultConfig()
			conf, err := SetMode(tt.mode, &defaultConf)
			if tt.expectedErrMsg != "" {
				require.EqualError(t, err, tt.expectedErrMsg)
				return
			}
			require.NoError(t, err)
			require.Exactly(t, tt.expectedConfig, *conf)
		})
	}
}

func TestCheckConfig(t *testing.T) {
	tests := []struct {
		name     string
		conf     func() *Config
		expected []string
	}{
		{
			name:     "shall return no errors when config is valid",
			conf:     getValidConfig,
			expected: []string{},
		},
		{
			name: "shall return an error when config file does not contain data directory setting",
			conf: func() *Config {
				c := getValidConfig()
				c.Redpanda.Directory = ""
				return c
			},
			expected: []string{"redpanda.data_directory can't be empty"},
		},
		{
			name: "shall return an error when id of server is negative",
			conf: func() *Config {
				c := getValidConfig()
				c.Redpanda.Id = -100
				return c
			},
			expected: []string{"redpanda.node_id can't be a negative integer"},
		},
		{
			name: "shall return an error when the RPC server port is 0",
			conf: func() *Config {
				c := getValidConfig()
				c.Redpanda.RPCServer.Port = 0
				return c
			},
			expected: []string{"redpanda.rpc_server.port can't be 0"},
		},
		{
			name: "shall return an error when the RPC server address is empty",
			conf: func() *Config {
				c := getValidConfig()
				c.Redpanda.RPCServer.Address = ""
				return c
			},
			expected: []string{"redpanda.rpc_server.address can't be empty"},
		},
		{
			name: "shall return an error when the Kafka API port is 0",
			conf: func() *Config {
				c := getValidConfig()
				c.Redpanda.KafkaApi.Port = 0
				return c
			},
			expected: []string{"redpanda.kafka_api.port can't be 0"},
		},
		{
			name: "shall return an error when the Kafka API address is empty",
			conf: func() *Config {
				c := getValidConfig()
				c.Redpanda.KafkaApi.Address = ""
				return c
			},
			expected: []string{"redpanda.kafka_api.address can't be empty"},
		},
		{
			name: "shall return an error when one of the seed servers' address is empty",
			conf: func() *Config {
				c := getValidConfig()
				c.Redpanda.SeedServers[0].Host.Address = ""
				return c
			},
			expected: []string{"redpanda.seed_servers.0.host.address can't be empty"},
		},
		{
			name: "shall return an error when one of the seed servers' port is 0",
			conf: func() *Config {
				c := getValidConfig()
				c.Redpanda.SeedServers[1].Host.Port = 0
				return c
			},
			expected: []string{"redpanda.seed_servers.1.host.port can't be 0"},
		},
		{
			name: "shall return no errors when tune_coredump is set to false," +
				"regardless of coredump_dir's value",
			conf: func() *Config {
				c := getValidConfig()
				c.Rpk.TuneCoredump = false
				c.Rpk.CoredumpDir = ""
				return c
			},
			expected: []string{},
		},
		{
			name: "shall return an error when tune_coredump is set to true," +
				"but coredump_dir is empty",
			conf: func() *Config {
				c := getValidConfig()
				c.Rpk.CoredumpDir = ""
				return c
			},
			expected: []string{"if rpk.tune_coredump is set to true," +
				"rpk.coredump_dir can't be empty"},
		},
		{
			name: "shall return no error if setup is empty," +
				"but coredump_dir is empty",
			conf: func() *Config {
				c := getValidConfig()
				c.Rpk.WellKnownIo = ""
				return c
			},
			expected: []string{},
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, got := CheckConfig(tt.conf())
			errMsgs := []string{}
			for _, err := range got {
				errMsgs = append(errMsgs, err.Error())
			}
			require.Exactly(t, tt.expected, errMsgs)
		})
	}
}

func TestReadAsJSON(t *testing.T) {
	tests := []struct {
		name           string
		before         func(fs afero.Fs) error
		path           string
		expected       string
		expectedErrMsg string
	}{
		{
			name: "it should load the config as JSON",
			before: func(fs afero.Fs) error {
				conf := DefaultConfig()
				return WriteConfig(fs, &conf, conf.ConfigFile)
			},
			path:     DefaultConfig().ConfigFile,
			expected: `{"config_file":"/etc/redpanda/redpanda.yaml","redpanda":{"admin":{"address":"0.0.0.0","port":9644},"data_directory":"/var/lib/redpanda/data","kafka_api":{"address":"0.0.0.0","port":9092},"kafka_api_tls":{"cert_file":"","enabled":false,"key_file":"","truststore_file":""},"node_id":0,"rpc_server":{"address":"0.0.0.0","port":33145},"seed_servers":[]},"rpk":{"coredump_dir":"/var/lib/redpanda/coredump","enable_memory_locking":false,"enable_usage_stats":false,"tls":{"cert_file":"","key_file":"","truststore_file":""},"tune_aio_events":false,"tune_clocksource":false,"tune_coredump":false,"tune_cpu":false,"tune_disk_irq":false,"tune_disk_nomerges":false,"tune_disk_scheduler":false,"tune_fstrim":false,"tune_network":false,"tune_swappiness":false,"tune_transparent_hugepages":false}}`,
		},
		{
			name:           "it should fail if the the config isn't found",
			path:           DefaultConfig().ConfigFile,
			expectedErrMsg: "open /etc/redpanda/redpanda.yaml: file does not exist",
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(st *testing.T) {
			fs := afero.NewMemMapFs()
			if tt.before != nil {
				require.NoError(st, tt.before(fs))
			}
			actual, err := ReadAsJSON(fs, tt.path)
			if tt.expectedErrMsg != "" {
				require.EqualError(st, err, tt.expectedErrMsg)
				return
			}
			require.NoError(st, err)
			require.Equal(st, tt.expected, actual)
		})
	}
}

func TestReadFlat(t *testing.T) {
	expected := map[string]string{
		"config_file":                            "/etc/redpanda/redpanda.yaml",
		"redpanda.admin":                         "0.0.0.0:9644",
		"redpanda.data_directory":                "/var/lib/redpanda/data",
		"redpanda.kafka_api":                     "0.0.0.0:9092",
		"redpanda.kafka_api_tls.cert_file":       "",
		"redpanda.kafka_api_tls.enabled":         "false",
		"redpanda.kafka_api_tls.key_file":        "",
		"redpanda.kafka_api_tls.truststore_file": "",
		"redpanda.node_id":                       "0",
		"redpanda.rpc_server":                    "0.0.0.0:33145",
		"redpanda.seed_servers.1000":             "192.168.167.0:1337",
		"redpanda.seed_servers.1001":             "192.168.167.1:1337",
		"rpk.coredump_dir":                       "/var/lib/redpanda/coredump",
		"rpk.enable_memory_locking":              "false",
		"rpk.enable_usage_stats":                 "false",
		"rpk.tls.cert_file":                      "",
		"rpk.tls.key_file":                       "",
		"rpk.tls.truststore_file":                "",
		"rpk.tune_aio_events":                    "false",
		"rpk.tune_clocksource":                   "false",
		"rpk.tune_coredump":                      "false",
		"rpk.tune_cpu":                           "false",
		"rpk.tune_disk_irq":                      "false",
		"rpk.tune_disk_nomerges":                 "false",
		"rpk.tune_disk_scheduler":                "false",
		"rpk.tune_fstrim":                        "false",
		"rpk.tune_network":                       "false",
		"rpk.tune_swappiness":                    "false",
		"rpk.tune_transparent_hugepages":         "false",
	}
	fs := afero.NewMemMapFs()
	conf := DefaultConfig()
	conf.Redpanda.SeedServers = []*SeedServer{
		&SeedServer{
			SocketAddress{"192.168.167.0", 1337},
			1000,
		}, &SeedServer{
			SocketAddress{"192.168.167.1", 1337},
			1001,
		},
	}
	err := WriteConfig(fs, &conf, conf.ConfigFile)
	require.NoError(t, err)
	props, err := ReadFlat(fs, conf.ConfigFile)
	require.NoError(t, err)
	require.NotEqual(t, 0, len(props))

	require.Exactly(t, expected, props)
}

func TestWriteAndGenerateNodeUuid(t *testing.T) {
	fs := afero.NewMemMapFs()
	baseDir := "/etc/redpanda"
	path := baseDir + "/redpanda.yaml"
	conf := getValidConfig()
	conf.ConfigFile = path
	bs, err := yaml.Marshal(conf)
	require.NoError(t, err)
	err = fs.MkdirAll(baseDir, 0755)
	require.NoError(t, err)
	_, err = utils.WriteBytes(fs, bs, path)
	require.NoError(t, err)
	conf, err = GenerateAndWriteNodeUuid(fs, conf)
	require.NoError(t, err)
	require.NotEqual(t, "", conf.NodeUuid)
	readConf, err := ReadConfigFromPath(fs, path)
	require.NoError(t, err)
	require.Exactly(t, conf, readConf)
}
