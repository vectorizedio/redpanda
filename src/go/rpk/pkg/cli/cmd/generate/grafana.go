package generate

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io/ioutil"
	"net/http"
	"os"
	"sort"
	"strings"
	"vectorized/pkg/cli"
	"vectorized/pkg/cli/cmd/generate/graf"

	dto "github.com/prometheus/client_model/go"
	"github.com/prometheus/common/expfmt"
	log "github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
)

var datasource string
var jobName string

const panelHeight = 6

var metricGroups = []string{
	"errors",
	"storage",
	"reactor",
	"scheduler",
	"io_queue",
	"vectorized_internal_rpc_protocol",
	"kafka_rpc_protocol",
	"rpc_client",
	"memory",
	"raft",
}

type RowSet struct {
	rowTitles   []string
	groupPanels map[string]*graf.RowPanel
}

func newRowSet() *RowSet {
	return &RowSet{
		rowTitles:   []string{},
		groupPanels: map[string]*graf.RowPanel{},
	}
}

func NewGrafanaDashboardCmd() *cobra.Command {
	var prometheusURL string
	command := &cobra.Command{
		Use:   "grafana-dashboard",
		Short: "Generate a Grafana dashboard for redpanda metrics.",
		RunE: func(ccmd *cobra.Command, args []string) error {
			if !(strings.HasPrefix(prometheusURL, "http://") ||
				strings.HasPrefix(prometheusURL, "https://")) {
				prometheusURL = fmt.Sprintf("http://%s", prometheusURL)
			}
			return executeGrafanaDashboard(prometheusURL)
		},
	}
	prometheusURLFlag := "prometheus-url"
	command.Flags().StringVar(
		&prometheusURL,
		prometheusURLFlag,
		"http://localhost:9644/metrics",
		"The redpanda Prometheus URL from where to get the metrics metadata")
	datasourceFlag := "datasource"
	command.Flags().StringVar(
		&datasource,
		datasourceFlag,
		"",
		"The name of the Prometheus datasource as configured in your grafana instance.")
	command.Flags().StringVar(
		&jobName,
		"job-name",
		"redpanda",
		"The prometheus job name by which to identify the redpanda nodes")
	command.MarkFlagRequired(datasourceFlag)
	return command
}

func executeGrafanaDashboard(prometheusURL string) error {
	metricFamilies, err := fetchMetrics(prometheusURL)
	if err != nil {
		return err
	}
	dashboard := buildGrafanaDashboard(metricFamilies)
	jsonSpec, err := json.MarshalIndent(dashboard, "", " ")
	if err != nil {
		return err
	}
	log.SetFormatter(cli.NewNoopFormatter())
	// The logger's default stream is stderr, which prevents piping to files
	// from working without redirecting them with '2>&1'.
	if log.StandardLogger().Out == os.Stderr {
		log.SetOutput(os.Stdout)
	}
	log.Info(string(jsonSpec))
	return nil
}

func buildGrafanaDashboard(
	metricFamilies map[string]*dto.MetricFamily,
) graf.Dashboard {
	intervals := []string{"5s", "10s", "30s", "1m", "5m", "15m", "30m", "1h", "2h", "1d"}
	timeOptions := []string{"5m", "15m", "1h", "6h", "12h", "24h", "2d", "7d", "30d"}
	summaryPanels := buildSummary(metricFamilies, jobName)
	lastY := summaryPanels[len(summaryPanels)-1].GetGridPos().Y + panelHeight
	rowSet := newRowSet()
	rowSet.processRows(metricFamilies)
	rows := rowSet.finalize(lastY)
	return graf.Dashboard{
		Title:      "Redpanda",
		Templating: buildTemplating(),
		Panels: append(
			summaryPanels,
			rows...,
		),
		Editable: true,
		Refresh:  "10s",
		Time:     graf.Time{From: "now-1h", To: "now"},
		TimePicker: graf.TimePicker{
			RefreshIntervals: intervals,
			TimeOptions:      timeOptions,
		},
		Timezone:      "utc",
		SchemaVersion: 12,
	}
}

func (rowSet *RowSet) finalize(fromY int) []graf.Panel {
	panelWidth := 8

	sort.Strings(rowSet.rowTitles)
	rows := []graf.Panel{}

	y := fromY
	for _, title := range rowSet.rowTitles {
		row := rowSet.groupPanels[title]
		row.GetGridPos().Y = y
		for i, panel := range row.Panels {
			panel.GetGridPos().Y = y
			panel.GetGridPos().X = (i * panelWidth) % 24
		}
		rows = append(rows, row)
		y++
	}

	return rows
}

func (rowSet *RowSet) processRows(metricFamilies map[string]*dto.MetricFamily) {
	names := []string{}
	for k, _ := range metricFamilies {
		names = append(names, k)
	}
	sort.Strings(names)
	for _, name := range names {
		family := metricFamilies[name]
		var panel graf.Panel
		if family.GetType() == dto.MetricType_COUNTER {
			panel = newCounterPanel(family)
		} else if subtype(family) == "histogram" {
			panel = newPercentilePanel(family, 0.95)
		} else {
			panel = newGaugePanel(family)
		}

		if panel == nil {
			continue
		}

		group := metricGroup(name)
		row, ok := rowSet.groupPanels[group]
		if ok {
			row.Panels = append(row.Panels, panel)
			rowSet.groupPanels[group] = row
		} else {
			rowSet.rowTitles = append(rowSet.rowTitles, group)
			rowSet.groupPanels[group] = graf.NewRowPanel(group, panel)
		}
	}
}

func buildTemplating() graf.Templating {
	node := newDefaultTemplateVar("node", "Node", true)
	node.IncludeAll = true
	node.AllValue = ".*"
	node.Type = "query"
	node.Query = "label_values(instance)"
	shard := newDefaultTemplateVar("node_shard", "Shard", true)
	shard.IncludeAll = true
	shard.AllValue = ".*"
	shard.Type = "query"
	shard.Query = "label_values(shard)"
	clusterOpt := graf.Option{
		Text:     "Cluster",
		Value:    "",
		Selected: false,
	}
	aggregateOpts := []graf.Option{
		clusterOpt,
		graf.Option{
			Text:     "Instance",
			Value:    "instance,",
			Selected: false,
		},
		graf.Option{
			Text:     "Instance, Shard",
			Value:    "instance,shard,",
			Selected: false,
		},
	}
	aggregate := newDefaultTemplateVar(
		"aggr_criteria",
		"Aggregate by",
		false,
		aggregateOpts...,
	)
	aggregate.Type = "custom"
	aggregate.Current = graf.Current{
		Text:  clusterOpt.Text,
		Value: clusterOpt.Value,
	}
	return graf.Templating{
		List: []graf.TemplateVar{node, shard, aggregate},
	}
}

func buildSummary(
	metricFamilies map[string]*dto.MetricFamily, jobName string,
) []graf.Panel {
	maxWidth := 24
	singleStatW := 2
	percentiles := []float32{0.95, 0.99}
	percentilesNo := len(percentiles)
	panels := []graf.Panel{}
	y := 0

	summaryText := htmlHeader("Redpanda Summary")
	summaryTitle := graf.NewTextPanel(summaryText, "html")
	summaryTitle.GridPos = graf.GridPos{H: 2, W: maxWidth, X: 0, Y: y}
	summaryTitle.Transparent = true
	panels = append(panels, summaryTitle)
	y += summaryTitle.GridPos.H

	nodesUp := graf.NewSingleStatPanel("Nodes Up")
	nodesUp.Datasource = datasource
	nodesUp.GridPos = graf.GridPos{H: 6, W: singleStatW, X: 0, Y: y}
	nodesUp.Targets = []graf.Target{graf.Target{
		Expr:           fmt.Sprintf(`count(up{job="%s"})`, jobName),
		Step:           40,
		IntervalFactor: 1,
		LegendFormat:   "Nodes Up",
	}}
	nodesUp.Transparent = true
	panels = append(panels, nodesUp)
	y += nodesUp.GridPos.H

	partitionCount := graf.NewSingleStatPanel("Partitions")
	partitionCount.Datasource = datasource
	partitionCount.GridPos = graf.GridPos{
		H: 6,
		W: singleStatW,
		X: nodesUp.GridPos.W,
		Y: y,
	}
	partitionCount.Targets = []graf.Target{graf.Target{
		Expr:         `sum(vectorized_raft_leader_for)`,
		LegendFormat: "Partition count",
	}}
	partitionCount.Transparent = true
	panels = append(panels, partitionCount)
	y += partitionCount.GridPos.H

	kafkaFamily, kafkaExists := metricFamilies["vectorized_kafka_rpc_protocol_dispatch_handler_latency"]
	if kafkaExists {
		width := (maxWidth - (singleStatW * 2)) / percentilesNo
		for i, p := range percentiles {
			panel := newPercentilePanel(kafkaFamily, p)
			panel.GridPos = graf.GridPos{
				H: panelHeight,
				W: width,
				X: i*width + (singleStatW * 2),
				Y: y,
			}
			panels = append(panels, panel)
		}
		y += panelHeight
	}
	width := maxWidth / 4
	rpcLatencyText := htmlHeader("Internal RPC Latency")
	rpcLatencyTitle := graf.NewTextPanel(rpcLatencyText, "html")
	rpcLatencyTitle.GridPos = graf.GridPos{H: 2, W: maxWidth / 2, X: 0, Y: y}
	rpcLatencyTitle.Transparent = true
	rpcFamily, rpcExists := metricFamilies["vectorized_vectorized_internal_rpc_protocol_dispatch_handler_latency"]
	if rpcExists {
		y += rpcLatencyTitle.GridPos.H
		panels = append(panels, rpcLatencyTitle)
		for i, p := range percentiles {
			panel := newPercentilePanel(rpcFamily, p)
			panel.GridPos = graf.GridPos{
				H: panelHeight,
				W: width,
				X: i * width,
				Y: y,
			}
			panels = append(panels, panel)
		}
	}

	throughputText := htmlHeader("Throughput")
	throughputTitle := graf.NewTextPanel(throughputText, "html")
	throughputTitle.GridPos = graf.GridPos{
		H: 2,
		W: maxWidth / 2,
		X: rpcLatencyTitle.GridPos.W,
		Y: rpcLatencyTitle.GridPos.Y,
	}
	throughputTitle.Transparent = true
	panels = append(panels, throughputTitle)

	readBytesFamily, readBytesExist := metricFamilies["vectorized_storage_log_read_bytes"]
	writtenBytesFamily, writtenBytesExist := metricFamilies["vectorized_storage_log_written_bytes"]
	if readBytesExist && writtenBytesExist {
		readPanel := newCounterPanel(readBytesFamily)
		readPanel.GridPos = graf.GridPos{
			H: panelHeight,
			W: width,
			X: maxWidth / 2,
			Y: y,
		}
		panels = append(panels, readPanel)

		writtenPanel := newCounterPanel(writtenBytesFamily)
		writtenPanel.GridPos = graf.GridPos{
			H: panelHeight,
			W: width,
			X: readPanel.GridPos.X + readPanel.GridPos.W,
			Y: y,
		}
		panels = append(panels, writtenPanel)
	}

	return panels
}

func metricGroup(metric string) string {
	for _, group := range metricGroups {
		if strings.Contains(metric, group) {
			return group
		}
	}
	return "others"
}

func fetchMetrics(prometheusURL string) (map[string]*dto.MetricFamily, error) {
	res, err := http.Get(prometheusURL)
	if err != nil {
		return nil, err
	}
	defer res.Body.Close()

	if res.StatusCode != 200 {
		return nil, fmt.Errorf(
			"the request to %s failed. Status: %d",
			prometheusURL,
			res.StatusCode,
		)
	}
	bs, err := ioutil.ReadAll(res.Body)
	if err != nil {
		return nil, err
	}
	parser := &expfmt.TextParser{}
	return parser.TextToMetricFamilies(bytes.NewBuffer(bs))
}

func newPercentilePanel(
	m *dto.MetricFamily, percentile float32,
) *graf.GraphPanel {
	expr := fmt.Sprintf(
		`histogram_quantile(%.2f, sum(rate(%s_bucket{instance=~"[[node]]",shard=~"[[node_shard]]"}[1m])) by (le, [[aggr_criteria]]))`,
		percentile,
		m.GetName(),
	)
	target := graf.Target{
		Expr:           expr,
		LegendFormat:   legendFormat(m),
		Format:         "time_series",
		Step:           10,
		IntervalFactor: 2,
		RefID:          "A",
	}
	title := fmt.Sprintf("%s (p%.0f)", m.GetHelp(), percentile*100)
	panel := newGraphPanel(title, target, "µs")
	panel.Lines = true
	panel.SteppedLine = true
	panel.NullPointMode = "null as zero"
	panel.Tooltip.ValueType = "individual"
	panel.Tooltip.Sort = 0
	return panel
}

func newCounterPanel(m *dto.MetricFamily) *graf.GraphPanel {
	expr := fmt.Sprintf(
		`sum(irate(%s{instance=~"[[node]]",shard=~"[[node_shard]]"}[1m])) by ([[aggr_criteria]])`,
		m.GetName(),
	)
	target := graf.Target{
		Expr:           expr,
		LegendFormat:   legendFormat(m),
		Format:         "time_series",
		Step:           10,
		IntervalFactor: 2,
	}
	format := "ops"
	if strings.Contains(m.GetName(), "bytes") {
		format = "Bps"
	}
	panel := newGraphPanel("Rate - "+m.GetHelp(), target, format)
	panel.Lines = true
	return panel
}

func newGaugePanel(m *dto.MetricFamily) *graf.GraphPanel {
	expr := fmt.Sprintf(
		`sum(%s{instance=~"[[node]]",shard=~"[[node_shard]]"}) by ([[aggr_criteria]])`,
		m.GetName(),
	)
	target := graf.Target{
		Expr:           expr,
		LegendFormat:   legendFormat(m),
		Format:         "time_series",
		Step:           10,
		IntervalFactor: 2,
	}
	format := "short"
	if strings.Contains(subtype(m), "bytes") {
		format = "bytes"
	}
	panel := newGraphPanel(m.GetHelp(), target, format)
	panel.Lines = true
	panel.SteppedLine = true
	return panel
}

func newGraphPanel(
	title string, target graf.Target, yAxisFormat string,
) *graf.GraphPanel {
	// yAxisMin := 0.0
	p := graf.NewGraphPanel(title, yAxisFormat)
	p.Datasource = datasource
	p.Targets = []graf.Target{target}
	p.Tooltip = graf.Tooltip{
		MsResolution: true,
		Shared:       true,
		ValueType:    "cumulative",
	}
	return p
}

func newDefaultTemplateVar(
	name, label string, multi bool, opts ...graf.Option,
) graf.TemplateVar {
	return graf.TemplateVar{
		Name:       name,
		Datasource: datasource,
		Label:      label,
		Multi:      multi,
		Refresh:    1,
		Sort:       1,
		Options:    opts,
	}
}

func legendFormat(m *dto.MetricFamily) string {
	duplicate := func(s string, ls []string) bool {
		for _, l := range ls {
			if s == l {
				return true
			}
		}
		return false
	}
	labels := []string{}
	legend := "node: {{instance}}"
	for _, metric := range m.GetMetric() {
		for _, label := range metric.GetLabel() {
			name := label.GetName()
			if name != "type" && !duplicate(name, labels) {
				legend += fmt.Sprintf(
					", %s: {{%s}}",
					name,
					name,
				)
				labels = append(labels, name)
			}
		}
	}
	return legend
}

func subtype(m *dto.MetricFamily) string {
	for _, metric := range m.GetMetric() {
		for _, label := range metric.GetLabel() {
			if label.GetName() == "type" {
				return label.GetValue()
			}
		}
	}
	return "none"
}

func htmlHeader(str string) string {
	return fmt.Sprintf(
		"<h1 style=\"color:#87CEEB; border-bottom: 3px solid #87CEEB;\">%s</h1>",
		str,
	)
}
