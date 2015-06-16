while [ true ]
do
  python run_auth_test.py ../../../bins/opt/async_streaming_ping_pong_test sidrakesh@google.com
  python run_auth_test.py ../../../bins/opt/async_unary_ping_pong_test sidrakesh@google.com
  python run_auth_test.py ../../../bins/opt/sync_streaming_ping_pong_test sidrakesh@google.com
  python run_auth_test.py ../../../bins/opt/sync_unary_ping_pong_test sidrakesh@google.com
done