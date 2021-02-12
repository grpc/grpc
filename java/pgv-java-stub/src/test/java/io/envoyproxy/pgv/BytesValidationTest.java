package io.envoyproxy.pgv;

import com.google.protobuf.ByteString;
import com.google.re2j.Pattern;
import org.junit.Test;

import java.net.InetAddress;
import java.net.UnknownHostException;

import static org.assertj.core.api.Assertions.assertThatThrownBy;

public class BytesValidationTest {
    @Test
    public void lengthWorks() throws ValidationException {
        // Short
        assertThatThrownBy(() -> BytesValidation.length("x", ByteString.copyFromUtf8("ñįö"), 8)).isInstanceOf(ValidationException.class);
        // Same
        BytesValidation.length("x", ByteString.copyFromUtf8("ñįöxx"), 8);
        // Long
        assertThatThrownBy(() -> BytesValidation.length("x", ByteString.copyFromUtf8("ñįöxxxx"), 8)).isInstanceOf(ValidationException.class);
    }

    @Test
    public void minLengthWorks() throws ValidationException {
        // Short
        assertThatThrownBy(() -> BytesValidation.minLength("x", ByteString.copyFromUtf8("ñįö"), 8)).isInstanceOf(ValidationException.class);
        // Same
        BytesValidation.minLength("x", ByteString.copyFromUtf8("ñįöxx"), 8);
        // Long
        BytesValidation.minLength("x", ByteString.copyFromUtf8("ñįöxxxx"), 8);
    }

    @Test
    public void maxLengthWorks() throws ValidationException {
        // Short
        BytesValidation.maxLength("x", ByteString.copyFromUtf8("ñįö"), 8);
        // Same
        BytesValidation.maxLength("x", ByteString.copyFromUtf8("ñįöxx"), 8);
        // Long
        assertThatThrownBy(() -> BytesValidation.maxLength("x", ByteString.copyFromUtf8("ñįöxxxx"), 8)).isInstanceOf(ValidationException.class);
    }

    @Test
    public void patternWorks() throws ValidationException {
        Pattern p = Pattern.compile("^[\\x00-\\x7F]+$");
        // Match
        BytesValidation.pattern("x", ByteString.copyFromUtf8("aaabbb"), p); // non-empty, ASCII byte sequence
        // No Match
        assertThatThrownBy(() -> BytesValidation.pattern("x", ByteString.copyFromUtf8("aaañbbb"), p)).isInstanceOf(ValidationException.class);
    }

    @Test
    public void prefixWorks() throws ValidationException {
        // Match
        BytesValidation.prefix("x", ByteString.copyFromUtf8("Hello World"), "Hello".getBytes());
        // No Match
        assertThatThrownBy(() -> BytesValidation.prefix("x", ByteString.copyFromUtf8("Hello World"), "Bananas".getBytes())).isInstanceOf(ValidationException.class);
    }

    @Test
    public void containsWorks() throws ValidationException {
        // Match
        BytesValidation.contains("x", ByteString.copyFromUtf8("Hello World"), "o W".getBytes());
        // No Match
        assertThatThrownBy(() -> BytesValidation.contains("x", ByteString.copyFromUtf8("Hello World"), "Bananas".getBytes())).isInstanceOf(ValidationException.class);
    }

    @Test
    public void suffixWorks() throws ValidationException {
        // Match
        BytesValidation.suffix("x", ByteString.copyFromUtf8("Hello World"), "World".getBytes());
        // No Match
        assertThatThrownBy(() -> BytesValidation.suffix("x", ByteString.copyFromUtf8("Hello World"), "Bananas".getBytes())).isInstanceOf(ValidationException.class);
    }

    @Test
    public void ipWorks() throws ValidationException, UnknownHostException {
        // Match
        BytesValidation.ip("x", ByteString.copyFrom(InetAddress.getByName("192.168.0.1").getAddress()));
        BytesValidation.ip("x", ByteString.copyFrom(InetAddress.getByName("fe80::3").getAddress()));
        // No Match
        assertThatThrownBy(() -> BytesValidation.ip("x", ByteString.copyFromUtf8("BANANAS!"))).isInstanceOf(ValidationException.class);
    }

    @Test
    public void ipV4Works() throws ValidationException, UnknownHostException {
        // Match
        BytesValidation.ipv4("x", ByteString.copyFrom(InetAddress.getByName("192.168.0.1").getAddress()));
        // No Match
        assertThatThrownBy(() -> BytesValidation.ipv4("x", ByteString.copyFrom(InetAddress.getByName("fe80::3").getAddress()))).isInstanceOf(ValidationException.class);
        assertThatThrownBy(() -> BytesValidation.ipv4("x", ByteString.copyFromUtf8("BANANAS!"))).isInstanceOf(ValidationException.class);
    }

    @Test
    public void ipV6Works() throws ValidationException, UnknownHostException {
        // Match
        BytesValidation.ipv6("x", ByteString.copyFrom(InetAddress.getByName("fe80::3").getAddress()));
        // No Match
        assertThatThrownBy(() -> BytesValidation.ipv6("x", ByteString.copyFrom(InetAddress.getByName("192.168.0.1").getAddress()))).isInstanceOf(ValidationException.class);
        assertThatThrownBy(() -> BytesValidation.ipv6("x", ByteString.copyFromUtf8("BANANAS!"))).isInstanceOf(ValidationException.class);
    }

    @Test
    public void inWorks() throws ValidationException {
        ByteString[] set = new ByteString[]{ByteString.copyFromUtf8("foo"), ByteString.copyFromUtf8("bar")};
        // In
        CollectiveValidation.in("x", ByteString.copyFromUtf8("foo"), set);
        // Not In
        assertThatThrownBy(() -> CollectiveValidation.in("x", ByteString.copyFromUtf8("baz"), set)).isInstanceOf(ValidationException.class);
    }

    @Test
    public void notInWorks() throws ValidationException {
        ByteString[] set = new ByteString[]{ByteString.copyFromUtf8("foo"), ByteString.copyFromUtf8("bar")};
        // In
        assertThatThrownBy(() -> CollectiveValidation.notIn("x", ByteString.copyFromUtf8("foo"), set)).isInstanceOf(ValidationException.class);
        // Not In
        CollectiveValidation.notIn("x", ByteString.copyFromUtf8("baz"), set);
    }
}
