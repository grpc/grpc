// Generated by the protocol buffer compiler.  DO NOT EDIT!
// source: src/proto/grpc/testing/metrics.proto
#pragma warning disable 1591, 0612, 3021
#region Designer generated code

using pb = global::Google.Protobuf;
using pbc = global::Google.Protobuf.Collections;
using pbr = global::Google.Protobuf.Reflection;
using scg = global::System.Collections.Generic;
namespace Grpc.Testing {

  /// <summary>Holder for reflection information generated from src/proto/grpc/testing/metrics.proto</summary>
  [global::System.Diagnostics.DebuggerNonUserCodeAttribute()]
  public static partial class MetricsReflection {

    #region Descriptor
    /// <summary>File descriptor for src/proto/grpc/testing/metrics.proto</summary>
    public static pbr::FileDescriptor Descriptor {
      get { return descriptor; }
    }
    private static pbr::FileDescriptor descriptor;

    static MetricsReflection() {
      byte[] descriptorData = global::System.Convert.FromBase64String(
          string.Concat(
            "CiRzcmMvcHJvdG8vZ3JwYy90ZXN0aW5nL21ldHJpY3MucHJvdG8SDGdycGMu",
            "dGVzdGluZyJsCg1HYXVnZVJlc3BvbnNlEgwKBG5hbWUYASABKAkSFAoKbG9u",
            "Z192YWx1ZRgCIAEoA0gAEhYKDGRvdWJsZV92YWx1ZRgDIAEoAUgAEhYKDHN0",
            "cmluZ192YWx1ZRgEIAEoCUgAQgcKBXZhbHVlIhwKDEdhdWdlUmVxdWVzdBIM",
            "CgRuYW1lGAEgASgJIg4KDEVtcHR5TWVzc2FnZTKgAQoOTWV0cmljc1NlcnZp",
            "Y2USSQoMR2V0QWxsR2F1Z2VzEhouZ3JwYy50ZXN0aW5nLkVtcHR5TWVzc2Fn",
            "ZRobLmdycGMudGVzdGluZy5HYXVnZVJlc3BvbnNlMAESQwoIR2V0R2F1Z2US",
            "Gi5ncnBjLnRlc3RpbmcuR2F1Z2VSZXF1ZXN0GhsuZ3JwYy50ZXN0aW5nLkdh",
            "dWdlUmVzcG9uc2ViBnByb3RvMw=="));
      descriptor = pbr::FileDescriptor.FromGeneratedCode(descriptorData,
          new pbr::FileDescriptor[] { },
          new pbr::GeneratedClrTypeInfo(null, new pbr::GeneratedClrTypeInfo[] {
            new pbr::GeneratedClrTypeInfo(typeof(global::Grpc.Testing.GaugeResponse), global::Grpc.Testing.GaugeResponse.Parser, new[]{ "Name", "LongValue", "DoubleValue", "StringValue" }, new[]{ "Value" }, null, null),
            new pbr::GeneratedClrTypeInfo(typeof(global::Grpc.Testing.GaugeRequest), global::Grpc.Testing.GaugeRequest.Parser, new[]{ "Name" }, null, null, null),
            new pbr::GeneratedClrTypeInfo(typeof(global::Grpc.Testing.EmptyMessage), global::Grpc.Testing.EmptyMessage.Parser, null, null, null, null)
          }));
    }
    #endregion

  }
  #region Messages
  /// <summary>
  ///  Reponse message containing the gauge name and value
  /// </summary>
  [global::System.Diagnostics.DebuggerNonUserCodeAttribute()]
  public sealed partial class GaugeResponse : pb::IMessage<GaugeResponse> {
    private static readonly pb::MessageParser<GaugeResponse> _parser = new pb::MessageParser<GaugeResponse>(() => new GaugeResponse());
    public static pb::MessageParser<GaugeResponse> Parser { get { return _parser; } }

    public static pbr::MessageDescriptor Descriptor {
      get { return global::Grpc.Testing.MetricsReflection.Descriptor.MessageTypes[0]; }
    }

    pbr::MessageDescriptor pb::IMessage.Descriptor {
      get { return Descriptor; }
    }

    public GaugeResponse() {
      OnConstruction();
    }

    partial void OnConstruction();

    public GaugeResponse(GaugeResponse other) : this() {
      name_ = other.name_;
      switch (other.ValueCase) {
        case ValueOneofCase.LongValue:
          LongValue = other.LongValue;
          break;
        case ValueOneofCase.DoubleValue:
          DoubleValue = other.DoubleValue;
          break;
        case ValueOneofCase.StringValue:
          StringValue = other.StringValue;
          break;
      }

    }

    public GaugeResponse Clone() {
      return new GaugeResponse(this);
    }

    /// <summary>Field number for the "name" field.</summary>
    public const int NameFieldNumber = 1;
    private string name_ = "";
    public string Name {
      get { return name_; }
      set {
        name_ = pb::ProtoPreconditions.CheckNotNull(value, "value");
      }
    }

    /// <summary>Field number for the "long_value" field.</summary>
    public const int LongValueFieldNumber = 2;
    public long LongValue {
      get { return valueCase_ == ValueOneofCase.LongValue ? (long) value_ : 0L; }
      set {
        value_ = value;
        valueCase_ = ValueOneofCase.LongValue;
      }
    }

    /// <summary>Field number for the "double_value" field.</summary>
    public const int DoubleValueFieldNumber = 3;
    public double DoubleValue {
      get { return valueCase_ == ValueOneofCase.DoubleValue ? (double) value_ : 0D; }
      set {
        value_ = value;
        valueCase_ = ValueOneofCase.DoubleValue;
      }
    }

    /// <summary>Field number for the "string_value" field.</summary>
    public const int StringValueFieldNumber = 4;
    public string StringValue {
      get { return valueCase_ == ValueOneofCase.StringValue ? (string) value_ : ""; }
      set {
        value_ = pb::ProtoPreconditions.CheckNotNull(value, "value");
        valueCase_ = ValueOneofCase.StringValue;
      }
    }

    private object value_;
    /// <summary>Enum of possible cases for the "value" oneof.</summary>
    public enum ValueOneofCase {
      None = 0,
      LongValue = 2,
      DoubleValue = 3,
      StringValue = 4,
    }
    private ValueOneofCase valueCase_ = ValueOneofCase.None;
    public ValueOneofCase ValueCase {
      get { return valueCase_; }
    }

    public void ClearValue() {
      valueCase_ = ValueOneofCase.None;
      value_ = null;
    }

    public override bool Equals(object other) {
      return Equals(other as GaugeResponse);
    }

    public bool Equals(GaugeResponse other) {
      if (ReferenceEquals(other, null)) {
        return false;
      }
      if (ReferenceEquals(other, this)) {
        return true;
      }
      if (Name != other.Name) return false;
      if (LongValue != other.LongValue) return false;
      if (DoubleValue != other.DoubleValue) return false;
      if (StringValue != other.StringValue) return false;
      if (ValueCase != other.ValueCase) return false;
      return true;
    }

    public override int GetHashCode() {
      int hash = 1;
      if (Name.Length != 0) hash ^= Name.GetHashCode();
      if (valueCase_ == ValueOneofCase.LongValue) hash ^= LongValue.GetHashCode();
      if (valueCase_ == ValueOneofCase.DoubleValue) hash ^= DoubleValue.GetHashCode();
      if (valueCase_ == ValueOneofCase.StringValue) hash ^= StringValue.GetHashCode();
      hash ^= (int) valueCase_;
      return hash;
    }

    public override string ToString() {
      return pb::JsonFormatter.ToDiagnosticString(this);
    }

    public void WriteTo(pb::CodedOutputStream output) {
      if (Name.Length != 0) {
        output.WriteRawTag(10);
        output.WriteString(Name);
      }
      if (valueCase_ == ValueOneofCase.LongValue) {
        output.WriteRawTag(16);
        output.WriteInt64(LongValue);
      }
      if (valueCase_ == ValueOneofCase.DoubleValue) {
        output.WriteRawTag(25);
        output.WriteDouble(DoubleValue);
      }
      if (valueCase_ == ValueOneofCase.StringValue) {
        output.WriteRawTag(34);
        output.WriteString(StringValue);
      }
    }

    public int CalculateSize() {
      int size = 0;
      if (Name.Length != 0) {
        size += 1 + pb::CodedOutputStream.ComputeStringSize(Name);
      }
      if (valueCase_ == ValueOneofCase.LongValue) {
        size += 1 + pb::CodedOutputStream.ComputeInt64Size(LongValue);
      }
      if (valueCase_ == ValueOneofCase.DoubleValue) {
        size += 1 + 8;
      }
      if (valueCase_ == ValueOneofCase.StringValue) {
        size += 1 + pb::CodedOutputStream.ComputeStringSize(StringValue);
      }
      return size;
    }

    public void MergeFrom(GaugeResponse other) {
      if (other == null) {
        return;
      }
      if (other.Name.Length != 0) {
        Name = other.Name;
      }
      switch (other.ValueCase) {
        case ValueOneofCase.LongValue:
          LongValue = other.LongValue;
          break;
        case ValueOneofCase.DoubleValue:
          DoubleValue = other.DoubleValue;
          break;
        case ValueOneofCase.StringValue:
          StringValue = other.StringValue;
          break;
      }

    }

    public void MergeFrom(pb::CodedInputStream input) {
      uint tag;
      while ((tag = input.ReadTag()) != 0) {
        switch(tag) {
          default:
            input.SkipLastField();
            break;
          case 10: {
            Name = input.ReadString();
            break;
          }
          case 16: {
            LongValue = input.ReadInt64();
            break;
          }
          case 25: {
            DoubleValue = input.ReadDouble();
            break;
          }
          case 34: {
            StringValue = input.ReadString();
            break;
          }
        }
      }
    }

  }

  /// <summary>
  ///  Request message containing the gauge name
  /// </summary>
  [global::System.Diagnostics.DebuggerNonUserCodeAttribute()]
  public sealed partial class GaugeRequest : pb::IMessage<GaugeRequest> {
    private static readonly pb::MessageParser<GaugeRequest> _parser = new pb::MessageParser<GaugeRequest>(() => new GaugeRequest());
    public static pb::MessageParser<GaugeRequest> Parser { get { return _parser; } }

    public static pbr::MessageDescriptor Descriptor {
      get { return global::Grpc.Testing.MetricsReflection.Descriptor.MessageTypes[1]; }
    }

    pbr::MessageDescriptor pb::IMessage.Descriptor {
      get { return Descriptor; }
    }

    public GaugeRequest() {
      OnConstruction();
    }

    partial void OnConstruction();

    public GaugeRequest(GaugeRequest other) : this() {
      name_ = other.name_;
    }

    public GaugeRequest Clone() {
      return new GaugeRequest(this);
    }

    /// <summary>Field number for the "name" field.</summary>
    public const int NameFieldNumber = 1;
    private string name_ = "";
    public string Name {
      get { return name_; }
      set {
        name_ = pb::ProtoPreconditions.CheckNotNull(value, "value");
      }
    }

    public override bool Equals(object other) {
      return Equals(other as GaugeRequest);
    }

    public bool Equals(GaugeRequest other) {
      if (ReferenceEquals(other, null)) {
        return false;
      }
      if (ReferenceEquals(other, this)) {
        return true;
      }
      if (Name != other.Name) return false;
      return true;
    }

    public override int GetHashCode() {
      int hash = 1;
      if (Name.Length != 0) hash ^= Name.GetHashCode();
      return hash;
    }

    public override string ToString() {
      return pb::JsonFormatter.ToDiagnosticString(this);
    }

    public void WriteTo(pb::CodedOutputStream output) {
      if (Name.Length != 0) {
        output.WriteRawTag(10);
        output.WriteString(Name);
      }
    }

    public int CalculateSize() {
      int size = 0;
      if (Name.Length != 0) {
        size += 1 + pb::CodedOutputStream.ComputeStringSize(Name);
      }
      return size;
    }

    public void MergeFrom(GaugeRequest other) {
      if (other == null) {
        return;
      }
      if (other.Name.Length != 0) {
        Name = other.Name;
      }
    }

    public void MergeFrom(pb::CodedInputStream input) {
      uint tag;
      while ((tag = input.ReadTag()) != 0) {
        switch(tag) {
          default:
            input.SkipLastField();
            break;
          case 10: {
            Name = input.ReadString();
            break;
          }
        }
      }
    }

  }

  [global::System.Diagnostics.DebuggerNonUserCodeAttribute()]
  public sealed partial class EmptyMessage : pb::IMessage<EmptyMessage> {
    private static readonly pb::MessageParser<EmptyMessage> _parser = new pb::MessageParser<EmptyMessage>(() => new EmptyMessage());
    public static pb::MessageParser<EmptyMessage> Parser { get { return _parser; } }

    public static pbr::MessageDescriptor Descriptor {
      get { return global::Grpc.Testing.MetricsReflection.Descriptor.MessageTypes[2]; }
    }

    pbr::MessageDescriptor pb::IMessage.Descriptor {
      get { return Descriptor; }
    }

    public EmptyMessage() {
      OnConstruction();
    }

    partial void OnConstruction();

    public EmptyMessage(EmptyMessage other) : this() {
    }

    public EmptyMessage Clone() {
      return new EmptyMessage(this);
    }

    public override bool Equals(object other) {
      return Equals(other as EmptyMessage);
    }

    public bool Equals(EmptyMessage other) {
      if (ReferenceEquals(other, null)) {
        return false;
      }
      if (ReferenceEquals(other, this)) {
        return true;
      }
      return true;
    }

    public override int GetHashCode() {
      int hash = 1;
      return hash;
    }

    public override string ToString() {
      return pb::JsonFormatter.ToDiagnosticString(this);
    }

    public void WriteTo(pb::CodedOutputStream output) {
    }

    public int CalculateSize() {
      int size = 0;
      return size;
    }

    public void MergeFrom(EmptyMessage other) {
      if (other == null) {
        return;
      }
    }

    public void MergeFrom(pb::CodedInputStream input) {
      uint tag;
      while ((tag = input.ReadTag()) != 0) {
        switch(tag) {
          default:
            input.SkipLastField();
            break;
        }
      }
    }

  }

  #endregion

}

#endregion Designer generated code
