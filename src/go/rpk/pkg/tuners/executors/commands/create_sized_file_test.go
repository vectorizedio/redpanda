// Copyright 2021 Vectorized, Inc.
//
// Use of this software is governed by the Business Source License
// included in the file licenses/BSL.md
//
// As of the Change Date specified in that file, in accordance with
// the Business Source License, use of this software will be governed
// by the Apache License, Version 2.0

package commands_test

import (
	"bufio"
	"bytes"
	"testing"

	"github.com/stretchr/testify/require"
	"github.com/vectorizedio/redpanda/src/go/rpk/pkg/tuners/executors/commands"
)

func TestWriteSizedFileCmdRender(t *testing.T) {
	cmd := commands.NewWriteSizedFileCmd(
		"/some/made/up/filepath.txt",
		int64(1),
	)

	expected := `if test -f "/some/made/up/filepath.txt"; then
  echo "file '/some/made/up/filepath.txt' already exists. If you're sure you want to replace it, remove it first and then run this tuner again."
else
  fallocate -l 1 /some/made/up/filepath.txt
fi
`
	var buf bytes.Buffer

	w := bufio.NewWriter(&buf)
	cmd.RenderScript(w)
	require.NoError(t, w.Flush())

	require.Equal(t, expected, buf.String())
}
