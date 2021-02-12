Stores, processes, explores and shares genomic data. This API implements
the Global Alliance for Genomics and Health (GA4GH) v0.5.1 API as well as
several extensions.

The Google Genomics API supports access via both
[JSON/REST](https://cloud.google.com/genomics/reference/rest) and
[gRPC](https://cloud.google.com/genomics/reference/rpc). JSON/REST is more
broadly available and is easier for getting started with Google Genomics; it
works well for small metadata resources (datasets, variant sets, read group
sets) and for browsing small genomic regions for datasets of any size. For
performant bulk data access (reads and variants), use gRPC.

See also an [overview of genomic resources](https://cloud.google.com/genomics/v1/users-guide)
and an overview of [Genomics on Google Cloud](https://cloud.google.com/genomics/overview).