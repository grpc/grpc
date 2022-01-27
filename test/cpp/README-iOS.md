## C++ tests on iOS

[GTMGoogleTestRunner](https://github.com/google/google-toolbox-for-mac/blob/master/UnitTesting/GTMGoogleTestRunner.mm) is used to convert googletest cases to XCTest that can be run on iOS. GTMGoogleTestRunner doesn't execute the `main` function, so we can't have any test logic in `main`.
However, it's ok to call `::testing::InitGoogleTest` in `main`, as `GTMGoogleTestRunner` [calls InitGoogleTest](https://github.com/google/google-toolbox-for-mac/blob/master/UnitTesting/GTMGoogleTestRunner.mm#L151).
`grpc::testing::TestEnvironment` can also be called from `main`, as it does some test initialization (install crash handler, seed RNG) that's not strictly required to run testcases on iOS.


## Porting exising C++ tests to run on iOS

Please follow these guidelines when porting tests to run on iOS:

- Tests need to use the googletest framework
- Any setup/teardown code in `main` needs to be moved to `SetUpTestCase`/`TearDownTestCase`, and `TEST` needs to be changed to `TEST_F`.
- [Death tests](https://github.com/google/googletest/blob/master/googletest/docs/advanced.md#death-tests) are not supported on iOS, so use the `*_IF_SUPPORTED()` macros to ensure that your code compiles on iOS.

For example, the following test
```c++
TEST(MyTest, TestOne) {
  ASSERT_DEATH(ThisShouldDie(), "");
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  return RUN_ALL_TESTS();
  grpc_shutdown();
}
```

should be changed to
```c++
class MyTest : public ::testing::Test {
 protected:
  static void SetUpTestCase() { grpc_init(); }
  static void TearDownTestCase() { grpc_shutdown(); }
};

TEST_F(MyTest, TestOne) {
  ASSERT_DEATH_IF_SUPPORTED(ThisShouldDie(), "");
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
```

## Limitations

Due to a [limitation](https://github.com/google/google-toolbox-for-mac/blob/master/UnitTesting/GTMGoogleTestRunner.mm#L48-L56) in GTMGoogleTestRunner, `SetUpTestCase`/`TeardownTestCase` will be called before/after *every* individual test case, similar to `SetUp`/`TearDown`.
