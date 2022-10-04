//
// Copyright 2020 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "src/core/lib/security/credentials/external/aws_request_signer.h"

#include <gmock/gmock.h>

#include <grpc/grpc_security.h>

#include "test/core/util/test_config.h"

namespace testing {

namespace {
// Test cases of Aws endpoints that the aws-sourced credentials will depend
// on.
const char* kAmzTestAccessKeyId = "ASIARD4OQDT6A77FR3CL";
const char* kAmzTestSecretAccessKey =
    "Y8AfSaucF37G4PpvfguKZ3/l7Id4uocLXxX0+VTx";
const char* kAmzTestToken =
    "IQoJb3JpZ2luX2VjEIz//////////wEaCXVzLWVhc3QtMiJGMEQCIH7MHX/Oy/"
    "OB8OlLQa9GrqU1B914+iMikqWQW7vPCKlgAiA/"
    "Lsv8Jcafn14owfxXn95FURZNKaaphj0ykpmS+Ki+"
    "CSq0AwhlEAAaDDA3NzA3MTM5MTk5NiIMx9sAeP1ovlMTMKLjKpEDwuJQg41/"
    "QUKx0laTZYjPlQvjwSqS3OB9P1KAXPWSLkliVMMqaHqelvMF/WO/"
    "glv3KwuTfQsavRNs3v5pcSEm4SPO3l7mCs7KrQUHwGP0neZhIKxEXy+Ls//1C/"
    "Bqt53NL+LSbaGv6RPHaX82laz2qElphg95aVLdYgIFY6JWV5fzyjgnhz0DQmy62/"
    "Vi8pNcM2/"
    "VnxeCQ8CC8dRDSt52ry2v+nc77vstuI9xV5k8mPtnaPoJDRANh0bjwY5Sdwkbp+"
    "mGRUJBAQRlNgHUJusefXQgVKBCiyJY4w3Csd8Bgj9IyDV+"
    "Azuy1jQqfFZWgP68LSz5bURyIjlWDQunO82stZ0BgplKKAa/"
    "KJHBPCp8Qi6i99uy7qh76FQAqgVTsnDuU6fGpHDcsDSGoCls2HgZjZFPeOj8mmRhFk1Xqvkb"
    "juz8V1cJk54d3gIJvQt8gD2D6yJQZecnuGWd5K2e2HohvCc8Fc9kBl1300nUJPV+k4tr/"
    "A5R/0QfEKOZL1/"
    "k5lf1g9CREnrM8LVkGxCgdYMxLQow1uTL+QU67AHRRSp5PhhGX4Rek+"
    "01vdYSnJCMaPhSEgcLqDlQkhk6MPsyT91QMXcWmyO+cAZwUPwnRamFepuP4K8k2KVXs/"
    "LIJHLELwAZ0ekyaS7CptgOqS7uaSTFG3U+vzFZLEnGvWQ7y9IPNQZ+"
    "Dffgh4p3vF4J68y9049sI6Sr5d5wbKkcbm8hdCDHZcv4lnqohquPirLiFQ3q7B17V9krMPu3"
    "mz1cg4Ekgcrn/"
    "E09NTsxAqD8NcZ7C7ECom9r+"
    "X3zkDOxaajW6hu3Az8hGlyylDaMiFfRbBJpTIlxp7jfa7CxikNgNtEKLH9iCzvuSg2vhA==";
const char* kAmzTestDate = "20200811T065522Z";

// Test cases derived from the Aws signature v4 test suite.
// https://github.com/boto/botocore/tree/master/tests/unit/auth/aws4_testsuite
const char* kBotoTestAccessKeyId = "AKIDEXAMPLE";
const char* kBotoTestSecretAccessKey =
    "wJalrXUtnFEMI/K7MDENG+bPxRfiCYEXAMPLEKEY";
const char* kBotoTestToken = "";
const char* kBotoTestDate = "Mon, 09 Sep 2011 23:36:00 GMT";
}  // namespace

// AWS official example from the developer doc.
// https://docs.aws.amazon.com/general/latest/gr/sigv4_signing.html
TEST(GrpcAwsRequestSignerTest, AWSOfficialExample) {
  grpc_error_handle error;
  grpc_core::AwsRequestSigner signer(
      "AKIDEXAMPLE", "wJalrXUtnFEMI/K7MDENG+bPxRfiCYEXAMPLEKEY", "", "GET",
      "https://iam.amazonaws.com/?Action=ListUsers&Version=2010-05-08",
      "us-east-1", "",
      {{"content-type", "application/x-www-form-urlencoded; charset=utf-8"},
       {"x-amz-date", "20150830T123600Z"}},
      &error);
  EXPECT_EQ(error, absl::OkStatus());
  EXPECT_EQ(signer.GetSignedRequestHeaders()["Authorization"],
            "AWS4-HMAC-SHA256 "
            "Credential=AKIDEXAMPLE/20150830/us-east-1/iam/aws4_request, "
            "SignedHeaders=content-type;host;x-amz-date, "
            "Signature="
            "5d672d79c15b13162d9279b0855cfba6789a8edb4c82c400e06b5924a6f2b5d7");
}

TEST(GrpcAwsRequestSignerTest, GetDescribeRegions) {
  grpc_error_handle error;
  grpc_core::AwsRequestSigner signer(
      kAmzTestAccessKeyId, kAmzTestSecretAccessKey, kAmzTestToken, "GET",
      "https://"
      "ec2.us-east-2.amazonaws.com?Action=DescribeRegions&Version=2013-10-15",
      "us-east-2", "", {{"x-amz-date", kAmzTestDate}}, &error);
  EXPECT_EQ(error, absl::OkStatus());
  EXPECT_EQ(
      signer.GetSignedRequestHeaders()["Authorization"],
      "AWS4-HMAC-SHA256 "
      "Credential=ASIARD4OQDT6A77FR3CL/20200811/us-east-2/ec2/aws4_request, "
      "SignedHeaders=host;x-amz-date;x-amz-security-token, "
      "Signature="
      "631ea80cddfaa545fdadb120dc92c9f18166e38a5c47b50fab9fce476e022855");
}

TEST(GrpcAwsRequestSignerTest, PostGetCallerIdentity) {
  grpc_error_handle error;
  grpc_core::AwsRequestSigner signer(
      kAmzTestAccessKeyId, kAmzTestSecretAccessKey, kAmzTestToken, "POST",
      "https://"
      "sts.us-east-2.amazonaws.com?Action=GetCallerIdentity&Version=2011-06-15",
      "us-east-2", "", {{"x-amz-date", kAmzTestDate}}, &error);
  EXPECT_EQ(error, absl::OkStatus());
  EXPECT_EQ(
      signer.GetSignedRequestHeaders()["Authorization"],
      "AWS4-HMAC-SHA256 "
      "Credential=ASIARD4OQDT6A77FR3CL/20200811/us-east-2/sts/aws4_request, "
      "SignedHeaders=host;x-amz-date;x-amz-security-token, "
      "Signature="
      "73452984e4a880ffdc5c392355733ec3f5ba310d5e0609a89244440cadfe7a7a");
}

TEST(GrpcAwsRequestSignerTest, PostGetCallerIdentityNoToken) {
  grpc_error_handle error;
  grpc_core::AwsRequestSigner signer(
      kAmzTestAccessKeyId, kAmzTestSecretAccessKey, "", "POST",
      "https://"
      "sts.us-east-2.amazonaws.com?Action=GetCallerIdentity&Version=2011-06-15",
      "us-east-2", "", {{"x-amz-date", kAmzTestDate}}, &error);
  EXPECT_EQ(error, absl::OkStatus());
  EXPECT_EQ(
      signer.GetSignedRequestHeaders()["Authorization"],
      "AWS4-HMAC-SHA256 "
      "Credential=ASIARD4OQDT6A77FR3CL/20200811/us-east-2/sts/aws4_request, "
      "SignedHeaders=host;x-amz-date, "
      "Signature="
      "d095ba304919cd0d5570ba8a3787884ee78b860f268ed040ba23831d55536d56");
}

TEST(GrpcAwsRequestSignerTest, GetHost) {
  grpc_error_handle error;
  grpc_core::AwsRequestSigner signer(kBotoTestAccessKeyId,
                                     kBotoTestSecretAccessKey, kBotoTestToken,
                                     "GET", "https://host.foo.com", "us-east-1",
                                     "", {{"date", kBotoTestDate}}, &error);
  EXPECT_EQ(error, absl::OkStatus());
  EXPECT_EQ(signer.GetSignedRequestHeaders()["Authorization"],
            "AWS4-HMAC-SHA256 "
            "Credential=AKIDEXAMPLE/20110909/us-east-1/host/aws4_request, "
            "SignedHeaders=date;host, "
            "Signature="
            "b27ccfbfa7df52a200ff74193ca6e32d4b48b8856fab7ebf1c595d0670a7e470");
}

TEST(GrpcAwsRequestSignerTest, GetHostDuplicateQueryParam) {
  grpc_error_handle error;
  grpc_core::AwsRequestSigner signer(
      kBotoTestAccessKeyId, kBotoTestSecretAccessKey, kBotoTestToken, "GET",
      "https://host.foo.com/?foo=Zoo&foo=aha", "us-east-1", "",
      {{"date", kBotoTestDate}}, &error);
  EXPECT_EQ(error, absl::OkStatus());
  EXPECT_EQ(signer.GetSignedRequestHeaders()["Authorization"],
            "AWS4-HMAC-SHA256 "
            "Credential=AKIDEXAMPLE/20110909/us-east-1/host/aws4_request, "
            "SignedHeaders=date;host, "
            "Signature="
            "be7148d34ebccdc6423b19085378aa0bee970bdc61d144bd1a8c48c33079ab09");
}

TEST(GrpcAwsRequestSignerTest, PostWithUpperCaseHeaderKey) {
  grpc_error_handle error;
  grpc_core::AwsRequestSigner signer(
      kBotoTestAccessKeyId, kBotoTestSecretAccessKey, kBotoTestToken, "POST",
      "https://host.foo.com/", "us-east-1", "",
      {{"date", kBotoTestDate}, {"ZOO", "zoobar"}}, &error);
  EXPECT_EQ(error, absl::OkStatus());
  EXPECT_EQ(signer.GetSignedRequestHeaders()["Authorization"],
            "AWS4-HMAC-SHA256 "
            "Credential=AKIDEXAMPLE/20110909/us-east-1/host/aws4_request, "
            "SignedHeaders=date;host;zoo, "
            "Signature="
            "b7a95a52518abbca0964a999a880429ab734f35ebbf1235bd79a5de87756dc4a");
}

TEST(GrpcAwsRequestSignerTest, PostWithUpperCaseHeaderValue) {
  grpc_error_handle error;
  grpc_core::AwsRequestSigner signer(
      kBotoTestAccessKeyId, kBotoTestSecretAccessKey, kBotoTestToken, "POST",
      "https://host.foo.com/", "us-east-1", "",
      {{"date", kBotoTestDate}, {"zoo", "ZOOBAR"}}, &error);
  EXPECT_EQ(error, absl::OkStatus());
  EXPECT_EQ(signer.GetSignedRequestHeaders()["Authorization"],
            "AWS4-HMAC-SHA256 "
            "Credential=AKIDEXAMPLE/20110909/us-east-1/host/aws4_request, "
            "SignedHeaders=date;host;zoo, "
            "Signature="
            "273313af9d0c265c531e11db70bbd653f3ba074c1009239e8559d3987039cad7");
}

TEST(GrpcAwsRequestSignerTest, SignPostWithHeader) {
  grpc_error_handle error;
  grpc_core::AwsRequestSigner signer(
      kBotoTestAccessKeyId, kBotoTestSecretAccessKey, kBotoTestToken, "POST",
      "https://host.foo.com/", "us-east-1", "",
      {{"date", kBotoTestDate}, {"p", "phfft"}}, &error);
  EXPECT_EQ(error, absl::OkStatus());
  EXPECT_EQ(signer.GetSignedRequestHeaders()["Authorization"],
            "AWS4-HMAC-SHA256 "
            "Credential=AKIDEXAMPLE/20110909/us-east-1/host/aws4_request, "
            "SignedHeaders=date;host;p, "
            "Signature="
            "debf546796015d6f6ded8626f5ce98597c33b47b9164cf6b17b4642036fcb592");
}

TEST(GrpcAwsRequestSignerTest, PostWithBodyNoCustomHeaders) {
  grpc_error_handle error;
  grpc_core::AwsRequestSigner signer(
      kBotoTestAccessKeyId, kBotoTestSecretAccessKey, kBotoTestToken, "POST",
      "https://host.foo.com/", "us-east-1", "foo=bar",
      {{"date", kBotoTestDate},
       {"Content-Type", "application/x-www-form-urlencoded"}},
      &error);
  EXPECT_EQ(error, absl::OkStatus());
  EXPECT_EQ(signer.GetSignedRequestHeaders()["Authorization"],
            "AWS4-HMAC-SHA256 "
            "Credential=AKIDEXAMPLE/20110909/us-east-1/host/aws4_request, "
            "SignedHeaders=content-type;date;host, "
            "Signature="
            "5a15b22cf462f047318703b92e6f4f38884e4a7ab7b1d6426ca46a8bd1c26cbc");
}

TEST(GrpcAwsRequestSignerTest, SignPostWithQueryString) {
  grpc_error_handle error;
  grpc_core::AwsRequestSigner signer(
      kBotoTestAccessKeyId, kBotoTestSecretAccessKey, kBotoTestToken, "POST",
      "https://host.foo.com/?foo=bar", "us-east-1", "",
      {{"date", kBotoTestDate}}, &error);
  EXPECT_EQ(error, absl::OkStatus());
  EXPECT_EQ(signer.GetSignedRequestHeaders()["Authorization"],
            "AWS4-HMAC-SHA256 "
            "Credential=AKIDEXAMPLE/20110909/us-east-1/host/aws4_request, "
            "SignedHeaders=date;host, "
            "Signature="
            "b6e3b79003ce0743a491606ba1035a804593b0efb1e20a11cba83f8c25a57a92");
}

TEST(GrpcAwsRequestSignerTest, InvalidUrl) {
  grpc_error_handle error;
  grpc_core::AwsRequestSigner signer("access_key_id", "secret_access_key",
                                     "token", "POST", "invalid_url",
                                     "us-east-1", "", {}, &error);
  std::string actual_error_description;
  GPR_ASSERT(grpc_error_get_str(error,
                                grpc_core::StatusStrProperty::kDescription,
                                &actual_error_description));
  EXPECT_EQ(actual_error_description, "Invalid Aws request url.");
}

TEST(GrpcAwsRequestSignerTest, DuplicateRequestDate) {
  grpc_error_handle error;
  grpc_core::AwsRequestSigner signer(
      "access_key_id", "secret_access_key", "token", "POST", "invalid_url",
      "us-east-1", "", {{"date", kBotoTestDate}, {"x-amz-date", kAmzTestDate}},
      &error);
  std::string actual_error_description;
  GPR_ASSERT(grpc_error_get_str(error,
                                grpc_core::StatusStrProperty::kDescription,
                                &actual_error_description));
  EXPECT_EQ(actual_error_description,
            "Only one of {date, x-amz-date} can be specified, not both.");
}

}  // namespace testing

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
