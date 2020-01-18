# TODO: Flowerbox.

import grpc

DESCRIPTOR_KEY = 'DESCRIPTOR'

def get_servicer_class_name(service_descriptor):
    return service_descriptor.name + "Servicer"


class ReflectiveServicerType(type):
    """Metaclass for Servicer classes created at runtime from ServiceDescriptors
    """

    def __init__(cls, name, bases, namespace):
        if DESCRIPTOR_KEY not in namespace:
            return

        service_descriptor = namespace[DESCRIPTOR_KEY]

        def _unimplemented_method(self, request, context):
            context.set_code(grpc.StatusCode.UNIMPLEMENTED)
            context.set_details('Method not implemented!')
            raise NotImplementedError('Method not implemented!')

        # TODO: Attach the proto-level service comment to the servicer class.

        for method_descriptor in service_descriptor.methods:
            # TODO: Somehow get the proto-level comment attached to the
            # documentation for this function. Perhaps check out how functools.wraps
            # is implemented.
            setattr(cls, method_descriptor.name, _unimplemented_method)

    # TODO: Add a comprehensive __str__/__repr__


def get_servicer_type(service_descriptor):
    class GenericServicer(object, metaclass=ReflectiveServicerType):
        pass
    setattr(GenericServicer, DESCRIPTOR_KEY, service_descriptor)
    GenericServicer.__name__ = get_servicer_class_name(service_descriptor)
    # TODO: Instances of this currently print out the following:
    # <grpc._service_reflection.get_servicer_type.<locals>.GenericServicer object at 0x7fe66d85bfd0>
    # Not good enough.
    return GenericServicer


_ARITY_HANDLERS = {
    True: {
            True: 'stream_stream',
            False: 'stream_unary',
    },
    False: {
            True: 'unary_stream',
            False: 'unary_unary',
    }
}

def _get_arity(method_descriptor):
    client_streaming = (method_descriptor.client_streaming if method_descriptor.has_client_streaming() else False)
    server_streaming = (method_descriptor.server_streaming if method_descriptor.has_server_streaming() else False)
    return _ARITY_HANDLERS[client_streaming][server_streaming]


def _get_handler(method_descriptor, channel):
    multicallable_func = getattr(channel, _get_arity(method_descriptor))
    return multicallable_func(request_serializer=method_descriptor.input_type.FromString,
                              response_serializer=method_descriptor.output_type.ToString)


def get_stub_class_name(service_descriptor):
    return service_descriptor.name + "Stub"


class ReflectiveStubType(type):
    """Metaclass for Stub classes created at runtime from ServiceDescriptors
    """

    def __init__(cls, name, bases, namespace):
        if DESCRIPTOR_KEY not in namespace:
            return

        service_descriptor = namespace[DESCRIPTOR_KEY]
        def _stub_init(self, channel):
            for method_descriptor in getattr(self, DESCRIPTOR_KEY).methods:
                handler = _get_handler(method_descriptor, channel)
                setattr(self, method_descriptor.name, handler)

        cls.__init__ = _stub_init


    # TODO: Add a comprehensive __str__/__repr__

def get_stub_type(service_descriptor):
    class GenericStub(object, metaclass=ReflectiveStubType):
        pass
    setattr(GenericStub, DESCRIPTOR_KEY, service_descriptor)
    GenericStub.__name__ = get_stub_class_name(service_descriptor)
    return GenericStub


def get_servicer_addition_function_name(service_descriptor):
    return "add_{}_to_server".format(get_servicer_class_name(service_descriptor))


def get_servicer_addition_function(service_descriptor):
    def _add_servicer_to_server(servicer, server):
        rpc_method_handlers = {}
        for method_descriptor in service_descriptor.methods:
            method_handler_func_name = _get_arity(method_descriptor) + "_rpc_method_handler"
            method_handler_func = getattr(grpc, method_handler_name)
            method_name = method_descriptor.name
            rpc_method_handlers[method_name] = (
                    method_handler_func(getattr(servicer, method_name),
                                        request_serializer=method_descriptor.input_type.FromString,
                                        response_deserializer=method_descriptor.output_type.ToString))
        generic_handler = grpc.generic_handler(service_descriptor.full_name, rpc_method_handlers)
        server.add_generic_rpc_handlers((generic_handler,))
    _add_servicer_to_server.__name__ = get_servicer_addition_function_name(service_descriptor)
    return _add_servicer_to_server
