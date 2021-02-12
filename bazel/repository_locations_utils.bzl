def _format_version(s, version):
    return s.format(version = version, dash_version = version.replace(".", "-"), underscore_version = version.replace(".", "_"))

# Generate a "repository location specification" from raw repository
# specification. The information should match the format required by
# external_deps.bzl. This function mostly does interpolation of {version} in
# the repository info fields. This code should be capable of running in both
# Python and Starlark.
def load_repository_locations_spec(repository_locations_spec):
    locations = {}
    for key, location in repository_locations_spec.items():
        mutable_location = dict(location)
        locations[key] = mutable_location

        # Fixup with version information.
        if "version" in location:
            if "strip_prefix" in location:
                mutable_location["strip_prefix"] = _format_version(location["strip_prefix"], location["version"])
            mutable_location["urls"] = [_format_version(url, location["version"]) for url in location["urls"]]
    return locations
