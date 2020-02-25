# How to contribute

We definitely welcome your patches and contributions to gRPC! Please read the gRPC
organization's [governance rules](https://github.com/grpc/grpc-community/blob/master/governance.md)
and [contribution guidelines](https://github.com/grpc/grpc-community/blob/master/CONTRIBUTING.md) before proceeding.

If you are new to github, please start by reading [Pull Request
howto](https://help.github.com/articles/about-pull-requests/)

## Legal requirements

In order to protect both you and ourselves, you will need to sign the
[Contributor License
Agreement](https://identity.linuxfoundation.org/projects/cncf).

## Cloning the repository

Before starting any development work you will need a local copy of the gRPC repository.

## Guidelines for Pull Requests
How to get your contributions merged smoothly and quickly.
 
- Create **small PRs** that are narrowly focused on **addressing a single
  concern**.  We often times receive PRs that are trying to fix several things
  at a time, but only one fix is considered acceptable, nothing gets merged and
  both author's & review's time is wasted.  Create more PRs to address different
  concerns and everyone will be happy.
 
- For speculative changes, consider opening an issue and discussing it first.
  If you are suggesting a behavioral or API change, consider starting with a
  [gRFC proposal](https://github.com/grpc/proposal).
 
- Provide a good **PR description** as a record of **what** change is being made
  and **why** it was made.  Link to a GitHub issue if it exists.
 
- Don't fix code style and formatting unless you are already changing that line
  to address an issue.  PRs with irrelevant changes won't be merged.  If you do
  want to fix formatting or style, do that in a separate PR.

- If you are adding a new file, make sure it has the copyright message template
  at the top as a comment. You can copy over the message from an existing file
  and update the year.
 
- Unless your PR is trivial, you should expect there will be reviewer comments
  that you'll need to address before merging.  We expect you to be reasonably
  responsive to those comments, otherwise the PR will be closed after 2-3 weeks
  of inactivity.

- Maintain **clean commit history** and use **meaningful commit messages**.
  PRs with messy commit history are difficult to review and won't be merged.
  Use `rebase -i upstream/master` to curate your commit history and/or to
  bring in latest changes from master (but avoid rebasing in the middle of
  a code review).
 
- Keep your PR up to date with upstream/master (if there are merge conflicts,
  we can't really merge your change).
 
- **All tests need to be passing** before your change can be merged.
  We recommend you **run tests locally** before creating your PR to catch
  breakages early on (see `src/test`)
 
- Exceptions to the rules can be made if there's a compelling reason for doing
  so.

## Obtaining Commit Access
We grant Commit Access to contributors based on the following criteria:
* Sustained contribution to the gRPC project.
* Deep understanding of the areas contributed to, and good consideration of various reliability, usability and performance tradeoffs. 
* Contributions demonstrate that obtaining Commit Access will significantly reduce friction for the contributors or others. 

In addition to submitting PRs, a Contributor with Commit Access can:
* Review PRs and merge once other checks and criteria pass.
* Triage bugs and PRs and assign appropriate labels and reviewers. 

### Obtaining Commit Access without Code Contributions 
The [gRPC organization](https://github.com/grpc) is comprised of multiple repositories and commit access is usually restricted to one or more of these repositories. Some repositories such as the [grpc.github.io](https://github.com/grpc/grpc.github.io/) do not have code, but the same principle of sustained, high quality contributions, with a good understanding of the fundamentals, apply. 

