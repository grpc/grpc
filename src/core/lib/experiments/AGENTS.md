# Experiments

This directory contains the implementation of the gRPC experiments system.

## Overarching Purpose

The experiments system provides a mechanism for controlling the behavior of gRPC at runtime. It is used to enable or disable new features, to change the default values of configuration parameters, and to collect data on the performance of different implementations.

## Core Concepts

The experiments system is built around a simple key-value store. The keys are the names of the experiments, and the values are booleans that indicate whether the experiment is enabled or disabled. The experiments are defined in a YAML file, and they can be enabled or disabled at runtime via a flag.

## How to Add a New Experiment

1.  Add a new entry to the `experiments.yaml` file. The entry should include the name of the experiment, a brief description of what it does, and the default value.
2.  Run the `tools/codegen/core/gen_experiments.py` script to regenerate the `experiments.h` and `experiments.cc` files, and the `bazel/experiments.bzl` file in the grpc root.
3.  Use the `grpc_core::IsExperimentEnabled` function to check if the experiment is enabled.

## Files

*   **`config.h`, `config.cc`**: These files contain the core logic for managing gRPC experiments.
*   **`experiments.yaml`**: This file defines all of the available experiments.
*   **`experiments.h`, `experiments.cc`**: These files are generated from `experiments.yaml`. They contain the definitions of the experiment enums and the `g_experiment_metadata` array.

## Major Functions

*   `grpc_core::IsExperimentEnabled`: Checks if an experiment is enabled.
*   `grpc_core::ForceEnableExperiment`: Forces an experiment to be enabled or disabled. This is intended for testing purposes only.

## Notes

*   Experiments can be enabled at runtime by setting the `grpc_experiments` flag with a comma-separated list of experiment names.
*   The experiments system is a powerful tool for developing and testing new features in gRPC. It allows developers to enable or disable features at runtime, which can be useful for A/B testing, for canarying new features, and for debugging.
