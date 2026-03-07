// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>

#include "opentelemetry/sdk/trace/batch_span_processor_options.h"
#include "opentelemetry/sdk/trace/batch_span_processor_runtime_options.h"
#include "opentelemetry/sdk/trace/exporter.h"
#include "opentelemetry/sdk/trace/processor.h"
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace sdk
{
namespace trace
{

/**
 * Factory class for BatchSpanProcessor.
 */
class OPENTELEMETRY_EXPORT BatchSpanProcessorFactory
{
public:
  /**
   * Create a BatchSpanProcessor.
   */
  static std::unique_ptr<SpanProcessor> Create(std::unique_ptr<SpanExporter> &&exporter,
                                               const BatchSpanProcessorOptions &options);

  /**
   * Create a BatchSpanProcessor.
   */
  static std::unique_ptr<SpanProcessor> Create(
      std::unique_ptr<SpanExporter> &&exporter,
      const BatchSpanProcessorOptions &options,
      const BatchSpanProcessorRuntimeOptions &runtime_options);
};

}  // namespace trace
}  // namespace sdk
OPENTELEMETRY_END_NAMESPACE
