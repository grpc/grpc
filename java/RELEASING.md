# Releasing PGV Java components

These steps are for releasing the Java components of PGV:
- pgv-java-stub
- pgv-java-grpc
- pgv-artifacts

## Releasing from master using CI

Releasing from master is fully automated by CI, but can't release historic tags.
```
curl -X POST -H "Content-Type: application/json" -d '{
"build_parameters": {
    "CIRCLE_JOB": "javabuild", 
    "RELEASE": "<release-version>",
    "NEXT": "<next-version>-SNAPSHOT",
    "GIT_USER_EMAIL": "envoy-bot@users.noreply.github.com",
    "GIT_USER_NAME": "Via CircleCI"
}}' "https://circleci.com/api/v1.1/project/github/envoyproxy/protoc-gen-validate/tree/master?circle-token=<my-token>"
```

## Manually releasing from git history

Manually releasing from git history is a more involved process, but allows you
to release from any point in the history.

1. Create a new `release/x.y.z` branch at the point you want to release.
1. Copy `.circleci\settings.xml` to a scratch location.
1. Fill out the parameters in `settings.xml`. You will need a published GPG key
   for code signing and the EnvoyReleaseBot sonatype username and password.
1. Execute the release command, substituting the path to `settings.xml`, the
   `releaseVersion`, and the next `developmentVersion` (-SNAPSHOT).
1. Merge the release branch back into master.

```
mvn -B -s /path/to/settings.xml clean release:prepare release:perform -Darguments="-s /path/to/settings.xml" -DreleaseVersion=x.y.z -DdevelopmentVersion=x.y.z-SNAPSHOT -DscmCommentPrefix="java release: "
```