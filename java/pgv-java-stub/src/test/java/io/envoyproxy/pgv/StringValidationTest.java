package io.envoyproxy.pgv;

import com.google.re2j.Pattern;
import org.junit.Test;

import static io.envoyproxy.pgv.Assertions.assertValidationException;
import static io.envoyproxy.pgv.StringValidation.uuid;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

public class StringValidationTest {
    private static String repeat(final char c, final int n) {
        return new String(new char[n]).replace('\0', c);
    }

    @Test
    public void inWorks() throws ValidationException {
        String[] set = new String[]{"foo", "bar", "🙈"};
        // In
        CollectiveValidation.in("x", "🙈", set);
        // Not In
        assertThatThrownBy(() -> CollectiveValidation.in("x", "baz", set)).isInstanceOf(ValidationException.class);
    }

    @Test
    public void notInWorks() throws ValidationException {
        String[] set = new String[]{"foo", "bar", "🙈"};
        // In
        assertThatThrownBy(() -> CollectiveValidation.notIn("x", "🙈", set)).isInstanceOf(ValidationException.class);
        // Not In
        CollectiveValidation.notIn("x", "baz", set);
    }

    @Test
    public void lengthWorks() throws ValidationException {
        // Short
        assertThatThrownBy(() -> StringValidation.length("x", "🙈", 2)).isInstanceOf(ValidationException.class);
        // Same
        StringValidation.length("x", "🙈🙉", 2);
        // Long
        assertThatThrownBy(() -> StringValidation.length("x", "🙈🙉🙊", 2)).isInstanceOf(ValidationException.class);
    }

    @Test
    public void minLengthWorks() throws ValidationException {
        // Short
        assertThatThrownBy(() -> StringValidation.minLength("x", "🙈", 2)).isInstanceOf(ValidationException.class);
        // Same
        StringValidation.minLength("x", "🙈🙉", 2);
        // Long
        StringValidation.minLength("x", "🙈🙉🙊", 2);
    }

    @Test
    public void maxLengthWorks() throws ValidationException {
        // Short
        StringValidation.maxLength("x", "🙈", 2);
        // Same
        StringValidation.maxLength("x", "🙈🙉", 2);
        // Long
        assertThatThrownBy(() -> StringValidation.maxLength("x", "🙈🙉🙊", 2)).isInstanceOf(ValidationException.class);
    }

    @Test
    public void lengthBytesWorks() throws ValidationException {
        // Short
        assertThatThrownBy(() -> StringValidation.lenBytes("x", "ñįö", 8)).isInstanceOf(ValidationException.class);
        assertThatThrownBy(() -> StringValidation.lenBytes("x", "ñįö", 8)).isInstanceOf(ValidationException.class);
        // Same
        StringValidation.lenBytes("x", "ñįöxx", 8);
        // Long
        assertThatThrownBy(() -> StringValidation.lenBytes("x", "ñįöxxxx", 8)).isInstanceOf(ValidationException.class);
    }

    @Test
    public void minBytesWorks() throws ValidationException {
        // Short
        assertThatThrownBy(() -> StringValidation.minBytes("x", "ñįö", 8)).isInstanceOf(ValidationException.class);
        // Same
        StringValidation.minBytes("x", "ñįöxx", 8);
        StringValidation.minBytes("x", "你好", 4);
        // Long
        StringValidation.minBytes("x", "ñįöxxxx", 8);
    }

    @Test
    public void maxBytesWorks() throws ValidationException {
        // Short
        StringValidation.maxBytes("x", "ñįö", 8);
        // Same
        StringValidation.maxBytes("x", "ñįöxx", 8);
        // Long
        assertThatThrownBy(() -> StringValidation.maxBytes("x", "ñįöxxxx", 8)).isInstanceOf(ValidationException.class);
    }

    @Test
    public void patternWorks() throws ValidationException {
        Pattern p = Pattern.compile("a*b*");
        // Match
        StringValidation.pattern("x", "aaabbb", p);
        // No Match
        assertThatThrownBy(() -> StringValidation.pattern("x", "aaabbbccc", p)).isInstanceOf(ValidationException.class);
    }

    @Test
    public void patternWorks2() throws ValidationException {
        Pattern p = Pattern.compile("\\* \\\\ \\w");
        // Match
        StringValidation.pattern("x", "* \\ x", p);
    }

    @Test
    public void prefixWorks() throws ValidationException {
        // Match
        StringValidation.prefix("x", "Hello World", "Hello");
        // No Match
        assertThatThrownBy(() -> StringValidation.prefix("x", "Hello World", "Bananas")).isInstanceOf(ValidationException.class);
    }

    @Test
    public void containsWorks() throws ValidationException {
        // Match
        StringValidation.contains("x", "Hello World", "o W");
        // No Match
        assertThatThrownBy(() -> StringValidation.contains("x", "Hello World", "Bananas")).isInstanceOf(ValidationException.class);
    }

    @Test
    public void notContainsWorks() throws ValidationException {
        // Match
        StringValidation.notContains("x", "Hello World", "Bananas");
        // No Match
        assertThatThrownBy(() -> StringValidation.notContains("x", "Hello World", "o W")).isInstanceOf(ValidationException.class);
    }

    @Test
    public void suffixWorks() throws ValidationException {
        // Match
        StringValidation.suffix("x", "Hello World", "World");
        // No Match
        assertThatThrownBy(() -> StringValidation.suffix("x", "Hello World", "Bananas")).isInstanceOf(ValidationException.class);
    }

    @Test
    public void emailWorks() throws ValidationException {
        // Match
        StringValidation.email("x", "foo@bar.com");
        StringValidation.email("x", "John Smith <foo@bar.com>");
        StringValidation.email("x", "John Doe <john.\"we<i<>r>do\".doe@example.com>");
        // No Match
        assertThatThrownBy(() -> StringValidation.email("x", "bar.bar.bar")).isInstanceOf(ValidationException.class);
        assertThatThrownBy(() -> StringValidation.email("x", "John Doe <john.doe@example.com")).isInstanceOf(ValidationException.class);
        assertThatThrownBy(() -> StringValidation.email("x", "John Doe <john.doe@example.com> ")).isInstanceOf(ValidationException.class);
    }

    @Test
    public void hostNameWorks() throws ValidationException {
        // Match
        StringValidation.hostName("x", "google.com");
        // No Match
        assertThatThrownBy(() -> StringValidation.hostName("x", "bananas.bananas")).isInstanceOf(ValidationException.class);
        assertThatThrownBy(() -> StringValidation.hostName("x", "你好.com")).isInstanceOf(ValidationException.class);
    }

    @Test
    public void addressWorks() throws ValidationException {
        // Match Hostname
        StringValidation.address("x", "google.com");
        StringValidation.address("x", "images.google.com");
        // Match IP
        StringValidation.address("x", "127.0.0.1");
        StringValidation.address("x", "fe80::3");

        // No Match
        assertThatThrownBy(() -> StringValidation.address("x", "bananas.bananas")).isInstanceOf(ValidationException.class);
        assertThatThrownBy(() -> StringValidation.address("x", "你好.com")).isInstanceOf(ValidationException.class);
        assertThatThrownBy(() -> StringValidation.address("x", "ff::fff::0b")).isInstanceOf(ValidationException.class);
    }

    @Test
    public void ipWorks() throws ValidationException {
        // Match
        StringValidation.ip("x", "192.168.0.1");
        StringValidation.ip("x", "fe80::3");
        // No Match
        assertThatThrownBy(() -> StringValidation.ip("x", "999.999.999.999")).isInstanceOf(ValidationException.class);
    }

    @Test
    public void ipV4Works() throws ValidationException {
        // Match
        StringValidation.ipv4("x", "192.168.0.1");
        // No Match
        assertThatThrownBy(() -> StringValidation.ipv4("x", "fe80::3")).isInstanceOf(ValidationException.class);
        assertThatThrownBy(() -> StringValidation.ipv4("x", "999.999.999.999")).isInstanceOf(ValidationException.class);
    }

    @Test
    public void ipV6Works() throws ValidationException {
        // Match
        StringValidation.ipv6("x", "fe80::3");
        // No Match
        assertThatThrownBy(() -> StringValidation.ipv6("x", "192.168.0.1")).isInstanceOf(ValidationException.class);
        assertThatThrownBy(() -> StringValidation.ipv6("x", "999.999.999.999")).isInstanceOf(ValidationException.class);
    }

    @Test
    public void uriWorks() throws ValidationException {
        // Match
        StringValidation.uri("x", "ftp://ftp.is.co.za/rfc/rfc1808.txt");
        StringValidation.uri("x", "http://www.ietf.org/rfc/rfc2396.txt");
        StringValidation.uri("x", "ldap://[2001:db8::7]/c=GB?objectClass?one");
        StringValidation.uri("x", "mailto:John.Doe@example.com");
        StringValidation.uri("x", "news:comp.infosystems.www.servers.unix");
        StringValidation.uri("x", "telnet://192.0.2.16:80/");
        StringValidation.uri("x", "urn:oasis:names:specification:docbook:dtd:xml:4.1.2");
        StringValidation.uri("x", "tel:+1-816-555-1212");
        // No Match
        assertThatThrownBy(() -> StringValidation.uri("x", "server/resource")).isInstanceOf(ValidationException.class);
        assertThatThrownBy(() -> StringValidation.uri("x", "this is not a uri")).isInstanceOf(ValidationException.class);
    }

    @Test
    public void uriRefWorks() throws ValidationException {
        // Match
        StringValidation.uriRef("x", "server/resource");
        // No Match
        assertThatThrownBy(() -> StringValidation.uri("x", "this is not a uri")).isInstanceOf(ValidationException.class);
    }

    @Test
    public void uuidWorks() throws ValidationException {
        // We use this to generate UUIDs for all valid hex digits, so:
        // 00000000-0000…, 11111111-1111…, …, FFFFFFFF-FFFF…
        char[] chars = "0123456789abcdefABCDEF".toCharArray();

        // Match
        for (char c : chars) {
            final String s4 = repeat(c, 4);
            uuid(String.valueOf(c), repeat(c, 8) + '-' + s4 + '-' + s4 + '-' + s4 + '-' + repeat(c, 12));
        }

        // No Match
        assertValidationException(() -> uuid("g", "00000000-0000-0000-0000-00000000000g"));
        assertValidationException(() -> uuid("underscore", "00000000-0000_0000-0000-000000000000"));
        assertValidationException(() -> uuid("short", "00000000-000000000-0000-00000000000"));
        assertValidationException(() -> uuid("long", "00000000-000000000-0000-0000000000000"));
        assertValidationException(() -> uuid("1_dash_at_07", "0000000-00000-0000-0000-000000000000"));
        assertValidationException(() -> uuid("1_dash_at_09", "000000000-000-0000-0000-000000000000"));
        assertValidationException(() -> uuid("2_dash_at_12", "00000000-000-00000-0000-000000000000"));
        assertValidationException(() -> uuid("2_dash_at_14", "00000000-00000-000-0000-000000000000"));
        assertValidationException(() -> uuid("3_dash_at_17", "00000000-0000-000-00000-000000000000"));
        assertValidationException(() -> uuid("3_dash_at_19", "00000000-0000-00000-000-000000000000"));
        assertValidationException(() -> uuid("4_dash_at_22", "00000000-0000-0000-000-0000000000000"));
        assertValidationException(() -> uuid("4_dash_at_24", "00000000-0000-0000-00000-00000000000"));
    }
}
