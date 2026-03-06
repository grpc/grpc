# Copyright 2026 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Helper for gRPC experiments."""

def _val_to_mode(val):
    """Converts a rollout value to a build mode string."""
    if val == True:
        return "on"
    if val == False:
        return "off"
    if val == "debug":
        return "dbg"
    return None

def compute_experiment_data(experiments, rollouts):
    """Computes experiment data for the build system.

    Args:
        experiments: A list of dictionaries representing experiments.
        rollouts: A list of dictionaries representing rollouts.

    Returns:
        A struct containing:
            experiment_enables: A dict mapping experiment names to a comma-separated
                string of transitive dependencies (including itself).
            experiment_pollers: A list of experiment names that use polling.
            platform_config: A dict mapping platforms to a dict mapping modes
                to a dict mapping tags to a list of experiment names.
    """

    # 1. Initialize mapping of experiment name to its direct requirements.
    # We use dictionaries as sets for O(1) lookups and to avoid duplicates.
    requires_map = {}
    pollers = []
    for exp in experiments:
        name = exp["name"]
        requires_map[name] = {req: True for req in exp.get("requires", [])}
        if exp.get("uses_polling", False):
            pollers.append(name)

    # 2. Extend requirements based on rollout configurations.
    # Rollouts can specify additional dependencies for an experiment.
    for rollout in rollouts:
        name = rollout["name"]
        if name in requires_map:
            for req in rollout.get("requires", []):
                requires_map[name][req] = True

    # 3. Compute the transitive closure of requirements for each experiment.
    # Bounded loop for iterative expansion since Starlark doesn't support recursion.
    # Each experiment transitively requires itself.
    transitive_requires = {name: {name: True} for name in requires_map}

    for _ in range(20):
        changed = False
        for name in requires_map:
            current_transitive_deps = transitive_requires[name]

            # We use list() to avoid 'dictionary is modified during iteration' error.
            for dep in list(current_transitive_deps.keys()):
                if dep in requires_map:
                    for transitive_dep in requires_map[dep]:
                        if transitive_dep not in current_transitive_deps:
                            current_transitive_deps[transitive_dep] = True
                            changed = True
        if not changed:
            break

    # 4. Format the transitive closure as a dictionary of comma-separated strings.
    experiment_enables = {}
    for name, deps in transitive_requires.items():
        # Sort for determinism in the comma-separated string.
        experiment_enables[name] = ",".join(sorted(deps.keys()))

    # 5. Compute the platform config map.
    # format: platform -> mode -> tag -> list of experiments
    platform_config = {}
    rollout_map = {r["name"]: r.get("default", False) for r in rollouts}

    for exp in experiments:
        name = exp["name"]
        test_tags = exp.get("test_tags", [])
        if not test_tags:
            continue

        default = rollout_map.get(name, False)

        platforms_and_values = []
        if type(default) == type({}):
            for p, v in default.items():
                platforms_and_values.append((p, v))
        else:
            for p in ["windows", "ios", "posix"]:
                platforms_and_values.append((p, default))

        for platform, val in platforms_and_values:
            mode = _val_to_mode(val)
            if not mode:
                continue

            if platform not in platform_config:
                platform_config[platform] = {}
            if mode not in platform_config[platform]:
                platform_config[platform][mode] = {}

            for tag in test_tags:
                if tag not in platform_config[platform][mode]:
                    platform_config[platform][mode][tag] = []
                platform_config[platform][mode][tag].append(name)

    return struct(
        experiment_enables = experiment_enables,
        experiment_pollers = sorted(pollers),
        platform_config = platform_config,
    )
