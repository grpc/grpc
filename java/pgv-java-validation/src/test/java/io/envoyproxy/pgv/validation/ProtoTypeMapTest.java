package io.envoyproxy.pgv.validation;

import com.google.protobuf.Any;
import com.google.protobuf.InvalidProtocolBufferException;
import com.google.protobuf.Message;
import org.junit.Test;
import tests.harness.Harness;
import tests.harness.cases.Messages;
import tests.harness.cases.Numbers;

import java.util.Collections;

import static org.assertj.core.api.Assertions.assertThat;

public class ProtoTypeMapTest {
    @Test
    public void unpackAnyWorks() throws InvalidProtocolBufferException, ClassNotFoundException {
        Harness.TestCase testCase = Harness.TestCase.newBuilder()
                .setMessage(Any.pack(Numbers.UInt64GTLT.newBuilder().setVal(12345L).build()))
                .build();

        ProtoTypeMap typeMap = ProtoTypeMap.of(Collections.singletonList(Numbers.getDescriptor().toProto()));
        Message message = typeMap.unpackAny(testCase.getMessage());

        assertThat(message).isInstanceOf(Numbers.UInt64GTLT.class);
    }

    @Test
    public void unpackAnyNestedWorks() throws InvalidProtocolBufferException, ClassNotFoundException {
        Harness.TestCase testCase = Harness.TestCase.newBuilder()
                .setMessage(Any.pack(Messages.MessageNone.NoneMsg.newBuilder().build()))
                .build();

        ProtoTypeMap typeMap = ProtoTypeMap.of(Collections.singletonList(Messages.getDescriptor().toProto()));
        Message message = typeMap.unpackAny(testCase.getMessage());

        assertThat(message).isInstanceOf(Messages.MessageNone.NoneMsg.class);
    }
}
