from grpc.beta import implementations

# This code doesn't do much but makes sure the native extension is loaded
# which is what we are testing here.
channel = implementations.insecure_channel('localhost', 1000)
del channel
print 'Success!'
