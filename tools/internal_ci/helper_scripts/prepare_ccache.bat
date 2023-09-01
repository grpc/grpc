@rem Copyright 2022 The gRPC Authors
@rem
@rem Licensed under the Apache License, Version 2.0 (the "License");
@rem you may not use this file except in compliance with the License.
@rem You may obtain a copy of the License at
@rem
@rem     http://www.apache.org/licenses/LICENSE-2.0
@rem
@rem Unless required by applicable law or agreed to in writing, software
@rem distributed under the License is distributed on an "AS IS" BASIS,
@rem WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
@rem See the License for the specific language governing permissions and
@rem limitations under the License.

@rem Call this script to configure ccache (compiler cache to speed up builds)

@rem Allow use of ccache in builds that support that.
set GRPC_BUILD_ENABLE_CCACHE=true

@rem Redis instance housed in grpc-testing cloud project serves as the main compiler cache
set CCACHE_SECONDARY_STORAGE=redis://10.76.145.84:6379
