package tuners

import (
	"fmt"
	"vectorized/pkg/checkers"
)

func NewCheckedTunable(
	checker checkers.Checker,
	tuneAction func() TuneResult,
	supportedAction func() (supported bool, reason string),
) Tunable {
	return &checkedTunable{
		checker:         checker,
		tuneAction:      tuneAction,
		supportedAction: supportedAction,
	}
}

type checkedTunable struct {
	checker         checkers.Checker
	tuneAction      func() TuneResult
	supportedAction func() (supported bool, reason string)
}

func (t *checkedTunable) CheckIfSupported() (supported bool, reason string) {
	return t.supportedAction()
}

func (t *checkedTunable) Tune() TuneResult {

	result := t.checker.Check()
	if result.Err != nil {
		return NewTuneError(result.Err)
	}

	if result.IsOk {
		return NewTuneResult(false)
	}

	tuneResult := t.tuneAction()
	if tuneResult.GetError() != nil {
		return NewTuneError(tuneResult.GetError())
	}

	postTuneResult := t.checker.Check()
	if !postTuneResult.IsOk {
		err := fmt.Errorf("System tuning was not succesfull, "+
			"check '%s' failed, required: '%s', current '%v'",
			t.checker.GetDesc(),
			t.checker.GetRequiredAsString(), result.Current)
		return NewTuneError(err)
	}
	return tuneResult
}
