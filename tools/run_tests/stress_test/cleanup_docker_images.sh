#!/bin/bash
for img in `docker images | grep \<none\> | awk '{print  $3 }'` ; do docker rmi -f $img; done

