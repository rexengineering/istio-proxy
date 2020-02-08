// Copyright 2019 Istio Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package driver

import (
	"errors"
	"fmt"
	"log"
	"strings"
	"time"

	"github.com/d4l3k/messagediff"
	dto "github.com/prometheus/client_model/go"
	"github.com/prometheus/common/expfmt"

	"istio.io/proxy/test/envoye2e/env"
)

type Stats struct {
	AdminPort uint16
	Matchers  map[string]StatMatcher
}

type StatMatcher interface {
	Matches(*Params, *dto.MetricFamily) error
}

var _ Step = &Stats{}

func (s *Stats) Run(p *Params) error {
	for i := 0; i < 15; i++ {
		_, body, err := env.HTTPGet(fmt.Sprintf("http://127.0.0.1:%d/stats/prometheus", s.AdminPort))
		if err != nil {
			return err
		}
		reader := strings.NewReader(body)
		metrics, err := (&expfmt.TextParser{}).TextToMetricFamilies(reader)
		if err != nil {
			return err
		}
		count := 0
		for _, metric := range metrics {
			matcher, found := s.Matchers[metric.GetName()]
			if !found {
				continue
			}
			if err := matcher.Matches(p, metric); err == nil {
				log.Printf("matched metric %q", metric.GetName())
				count++
			} else {
				log.Printf("metric %q did not match: %v\n", metric.GetName(), err)
			}
		}
		if count == len(s.Matchers) {
			return nil
		}
		log.Printf("failed to match all metrics: want %#v", s.Matchers)
		time.Sleep(1 * time.Second)
	}
	return errors.New("failed to match all stats")
}

func (s *Stats) Cleanup() {}

type ExactStat struct {
	Metric string
}

func (me *ExactStat) Matches(params *Params, that *dto.MetricFamily) error {
	metric := &dto.MetricFamily{}
	params.LoadTestProto(me.Metric, metric)

	if s, same := messagediff.PrettyDiff(metric, that); !same {
		return fmt.Errorf("diff: %v, got: %v, want: %v", s, that, metric)
	}
	return nil
}

var _ StatMatcher = &ExactStat{}
