GCE images for GRPC
===================

This directory contains a number of shell files used for setting up GCE images
and instances for developing and testing gRPC.



Goal
----

- provides a script to create a GCE image that has everything needed to try
out gRPC on GCE.
- provide another script that creates a new GCE instance from the latest image

- additional scripts may be added in the future


Usage
------

# Minimal usage (see the scripts themselves for options)

$ create_grpc_dev_image.sh  # creates a grpc GCE image
$ ...
$ new_grpc_dev_instance.sh  # creates an instance using the latest grpc GCE image


Requirements
------------

Install [Google Cloud SDK](https://developers.google.com/cloud/sdk/)

Contents
--------

Library scripts that contain bash functions used in the other scripts:
- shared_setup_funcs.sh  # funcs used in create_grpc_dev_image and new_grpc_dev_instance
- gcutil_extras.sh  # wrappers for common tasks that us gcutil
- build_grpc_dist.sh  # funcs building the GRPC library and tests into a debian dist

GCE [startup scripts](https://developers.google.com/compute/docs/howtos/startupscript)
- *_on_startup.sh

Main scripts (as of 2014/09/04)
- create_grpc_dev_instance.sh
- new_grpc_dev_instance.sh

