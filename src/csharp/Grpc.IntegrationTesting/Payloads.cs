// Generated by the protocol buffer compiler.  DO NOT EDIT!
// source: src/proto/grpc/testing/payloads.proto
#pragma warning disable 1591, 0612, 3021
#region Designer generated code

using pb = global::Google.Protobuf;
using pbc = global::Google.Protobuf.Collections;
using pbr = global::Google.Protobuf.Reflection;
using scg = global::System.Collections.Generic;
namespace Grpc.Testing {

  /// <summary>Holder for reflection information generated from src/proto/grpc/testing/payloads.proto</summary>
  public static partial class PayloadsReflection {

    #region Descriptor
    /// <summary>File descriptor for src/proto/grpc/testing/payloads.proto</summary>
    public static pbr::FileDescriptor Descriptor {
      get { return descriptor; }
    }
    private static pbr::FileDescriptor descriptor;

    static PayloadsReflection() {
      byte[] descriptorData = global::System.Convert.FromBase64String(
          string.Concat(
            "CiVzcmMvcHJvdG8vZ3JwYy90ZXN0aW5nL3BheWxvYWRzLnByb3RvEgxncnBj",
            "LnRlc3RpbmciNwoQQnl0ZUJ1ZmZlclBhcmFtcxIQCghyZXFfc2l6ZRgBIAEo",
            "BRIRCglyZXNwX3NpemUYAiABKAUiOAoRU2ltcGxlUHJvdG9QYXJhbXMSEAoI",
            "cmVxX3NpemUYASABKAUSEQoJcmVzcF9zaXplGAIgASgFIhQKEkNvbXBsZXhQ",
            "cm90b1BhcmFtcyLKAQoNUGF5bG9hZENvbmZpZxI4Cg5ieXRlYnVmX3BhcmFt",
            "cxgBIAEoCzIeLmdycGMudGVzdGluZy5CeXRlQnVmZmVyUGFyYW1zSAASOAoN",
            "c2ltcGxlX3BhcmFtcxgCIAEoCzIfLmdycGMudGVzdGluZy5TaW1wbGVQcm90",
            "b1BhcmFtc0gAEjoKDmNvbXBsZXhfcGFyYW1zGAMgASgLMiAuZ3JwYy50ZXN0",
            "aW5nLkNvbXBsZXhQcm90b1BhcmFtc0gAQgkKB3BheWxvYWRiBnByb3RvMw=="));
      descriptor = pbr::FileDescriptor.FromGeneratedCode(descriptorData,
          new pbr::FileDescriptor[] { },
          new pbr::GeneratedClrTypeInfo(null, new pbr::GeneratedClrTypeInfo[] {
            new pbr::GeneratedClrTypeInfo(typeof(global::Grpc.Testing.ByteBufferParams), global::Grpc.Testing.ByteBufferParams.Parser, new[]{ "ReqSize", "RespSize" }, null, null, null),
            new pbr::GeneratedClrTypeInfo(typeof(global::Grpc.Testing.SimpleProtoParams), global::Grpc.Testing.SimpleProtoParams.Parser, new[]{ "ReqSize", "RespSize" }, null, null, null),
            new pbr::GeneratedClrTypeInfo(typeof(global::Grpc.Testing.ComplexProtoParams), global::Grpc.Testing.ComplexProtoParams.Parser, null, null, null, null),
            new pbr::GeneratedClrTypeInfo(typeof(global::Grpc.Testing.PayloadConfig), global::Grpc.Testing.PayloadConfig.Parser, new[]{ "BytebufParams", "SimpleParams", "ComplexParams" }, new[]{ "Payload" }, null, null)
          }));
    }
    #endregion

  }
  #region Messages
  public sealed partial class ByteBufferParams : pb::IMessage<ByteBufferParams> {
    private static readonly pb::MessageParser<ByteBufferParams> _parser = new pb::MessageParser<ByteBufferParams>(() => new ByteBufferParams());
    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public static pb::MessageParser<ByteBufferParams> Parser { get { return _parser; } }

    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public static pbr::MessageDescriptor Descriptor {
      get { return global::Grpc.Testing.PayloadsReflection.Descriptor.MessageTypes[0]; }
    }

    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    pbr::MessageDescriptor pb::IMessage.Descriptor {
      get { return Descriptor; }
    }

    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public ByteBufferParams() {
      OnConstruction();
    }

    partial void OnConstruction();

    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public ByteBufferParams(ByteBufferParams other) : this() {
      reqSize_ = other.reqSize_;
      respSize_ = other.respSize_;
    }

    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public ByteBufferParams Clone() {
      return new ByteBufferParams(this);
    }

    /// <summary>Field number for the "req_size" field.</summary>
    public const int ReqSizeFieldNumber = 1;
    private int reqSize_;
    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public int ReqSize {
      get { return reqSize_; }
      set {
        reqSize_ = value;
      }
    }

    /// <summary>Field number for the "resp_size" field.</summary>
    public const int RespSizeFieldNumber = 2;
    private int respSize_;
    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public int RespSize {
      get { return respSize_; }
      set {
        respSize_ = value;
      }
    }

    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public override bool Equals(object other) {
      return Equals(other as ByteBufferParams);
    }

    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public bool Equals(ByteBufferParams other) {
      if (ReferenceEquals(other, null)) {
        return false;
      }
      if (ReferenceEquals(other, this)) {
        return true;
      }
      if (ReqSize != other.ReqSize) return false;
      if (RespSize != other.RespSize) return false;
      return true;
    }

    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public override int GetHashCode() {
      int hash = 1;
      if (ReqSize != 0) hash ^= ReqSize.GetHashCode();
      if (RespSize != 0) hash ^= RespSize.GetHashCode();
      return hash;
    }

    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public override string ToString() {
      return pb::JsonFormatter.ToDiagnosticString(this);
    }

    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public void WriteTo(pb::CodedOutputStream output) {
      if (ReqSize != 0) {
        output.WriteRawTag(8);
        output.WriteInt32(ReqSize);
      }
      if (RespSize != 0) {
        output.WriteRawTag(16);
        output.WriteInt32(RespSize);
      }
    }

    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public int CalculateSize() {
      int size = 0;
      if (ReqSize != 0) {
        size += 1 + pb::CodedOutputStream.ComputeInt32Size(ReqSize);
      }
      if (RespSize != 0) {
        size += 1 + pb::CodedOutputStream.ComputeInt32Size(RespSize);
      }
      return size;
    }

    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public void MergeFrom(ByteBufferParams other) {
      if (other == null) {
        return;
      }
      if (other.ReqSize != 0) {
        ReqSize = other.ReqSize;
      }
      if (other.RespSize != 0) {
        RespSize = other.RespSize;
      }
    }

    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public void MergeFrom(pb::CodedInputStream input) {
      uint tag;
      while ((tag = input.ReadTag()) != 0) {
        switch(tag) {
          default:
            input.SkipLastField();
            break;
          case 8: {
            ReqSize = input.ReadInt32();
            break;
          }
          case 16: {
            RespSize = input.ReadInt32();
            break;
          }
        }
      }
    }

  }

  public sealed partial class SimpleProtoParams : pb::IMessage<SimpleProtoParams> {
    private static readonly pb::MessageParser<SimpleProtoParams> _parser = new pb::MessageParser<SimpleProtoParams>(() => new SimpleProtoParams());
    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public static pb::MessageParser<SimpleProtoParams> Parser { get { return _parser; } }

    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public static pbr::MessageDescriptor Descriptor {
      get { return global::Grpc.Testing.PayloadsReflection.Descriptor.MessageTypes[1]; }
    }

    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    pbr::MessageDescriptor pb::IMessage.Descriptor {
      get { return Descriptor; }
    }

    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public SimpleProtoParams() {
      OnConstruction();
    }

    partial void OnConstruction();

    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public SimpleProtoParams(SimpleProtoParams other) : this() {
      reqSize_ = other.reqSize_;
      respSize_ = other.respSize_;
    }

    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public SimpleProtoParams Clone() {
      return new SimpleProtoParams(this);
    }

    /// <summary>Field number for the "req_size" field.</summary>
    public const int ReqSizeFieldNumber = 1;
    private int reqSize_;
    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public int ReqSize {
      get { return reqSize_; }
      set {
        reqSize_ = value;
      }
    }

    /// <summary>Field number for the "resp_size" field.</summary>
    public const int RespSizeFieldNumber = 2;
    private int respSize_;
    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public int RespSize {
      get { return respSize_; }
      set {
        respSize_ = value;
      }
    }

    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public override bool Equals(object other) {
      return Equals(other as SimpleProtoParams);
    }

    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public bool Equals(SimpleProtoParams other) {
      if (ReferenceEquals(other, null)) {
        return false;
      }
      if (ReferenceEquals(other, this)) {
        return true;
      }
      if (ReqSize != other.ReqSize) return false;
      if (RespSize != other.RespSize) return false;
      return true;
    }

    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public override int GetHashCode() {
      int hash = 1;
      if (ReqSize != 0) hash ^= ReqSize.GetHashCode();
      if (RespSize != 0) hash ^= RespSize.GetHashCode();
      return hash;
    }

    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public override string ToString() {
      return pb::JsonFormatter.ToDiagnosticString(this);
    }

    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public void WriteTo(pb::CodedOutputStream output) {
      if (ReqSize != 0) {
        output.WriteRawTag(8);
        output.WriteInt32(ReqSize);
      }
      if (RespSize != 0) {
        output.WriteRawTag(16);
        output.WriteInt32(RespSize);
      }
    }

    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public int CalculateSize() {
      int size = 0;
      if (ReqSize != 0) {
        size += 1 + pb::CodedOutputStream.ComputeInt32Size(ReqSize);
      }
      if (RespSize != 0) {
        size += 1 + pb::CodedOutputStream.ComputeInt32Size(RespSize);
      }
      return size;
    }

    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public void MergeFrom(SimpleProtoParams other) {
      if (other == null) {
        return;
      }
      if (other.ReqSize != 0) {
        ReqSize = other.ReqSize;
      }
      if (other.RespSize != 0) {
        RespSize = other.RespSize;
      }
    }

    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public void MergeFrom(pb::CodedInputStream input) {
      uint tag;
      while ((tag = input.ReadTag()) != 0) {
        switch(tag) {
          default:
            input.SkipLastField();
            break;
          case 8: {
            ReqSize = input.ReadInt32();
            break;
          }
          case 16: {
            RespSize = input.ReadInt32();
            break;
          }
        }
      }
    }

  }

  /// <summary>
  /// TODO (vpai): Fill this in once the details of complex, representative
  ///              protos are decided
  /// </summary>
  public sealed partial class ComplexProtoParams : pb::IMessage<ComplexProtoParams> {
    private static readonly pb::MessageParser<ComplexProtoParams> _parser = new pb::MessageParser<ComplexProtoParams>(() => new ComplexProtoParams());
    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public static pb::MessageParser<ComplexProtoParams> Parser { get { return _parser; } }

    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public static pbr::MessageDescriptor Descriptor {
      get { return global::Grpc.Testing.PayloadsReflection.Descriptor.MessageTypes[2]; }
    }

    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    pbr::MessageDescriptor pb::IMessage.Descriptor {
      get { return Descriptor; }
    }

    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public ComplexProtoParams() {
      OnConstruction();
    }

    partial void OnConstruction();

    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public ComplexProtoParams(ComplexProtoParams other) : this() {
    }

    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public ComplexProtoParams Clone() {
      return new ComplexProtoParams(this);
    }

    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public override bool Equals(object other) {
      return Equals(other as ComplexProtoParams);
    }

    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public bool Equals(ComplexProtoParams other) {
      if (ReferenceEquals(other, null)) {
        return false;
      }
      if (ReferenceEquals(other, this)) {
        return true;
      }
      return true;
    }

    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public override int GetHashCode() {
      int hash = 1;
      return hash;
    }

    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public override string ToString() {
      return pb::JsonFormatter.ToDiagnosticString(this);
    }

    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public void WriteTo(pb::CodedOutputStream output) {
    }

    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public int CalculateSize() {
      int size = 0;
      return size;
    }

    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public void MergeFrom(ComplexProtoParams other) {
      if (other == null) {
        return;
      }
    }

    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
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

  public sealed partial class PayloadConfig : pb::IMessage<PayloadConfig> {
    private static readonly pb::MessageParser<PayloadConfig> _parser = new pb::MessageParser<PayloadConfig>(() => new PayloadConfig());
    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public static pb::MessageParser<PayloadConfig> Parser { get { return _parser; } }

    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public static pbr::MessageDescriptor Descriptor {
      get { return global::Grpc.Testing.PayloadsReflection.Descriptor.MessageTypes[3]; }
    }

    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    pbr::MessageDescriptor pb::IMessage.Descriptor {
      get { return Descriptor; }
    }

    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public PayloadConfig() {
      OnConstruction();
    }

    partial void OnConstruction();

    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public PayloadConfig(PayloadConfig other) : this() {
      switch (other.PayloadCase) {
        case PayloadOneofCase.BytebufParams:
          BytebufParams = other.BytebufParams.Clone();
          break;
        case PayloadOneofCase.SimpleParams:
          SimpleParams = other.SimpleParams.Clone();
          break;
        case PayloadOneofCase.ComplexParams:
          ComplexParams = other.ComplexParams.Clone();
          break;
      }

    }

    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public PayloadConfig Clone() {
      return new PayloadConfig(this);
    }

    /// <summary>Field number for the "bytebuf_params" field.</summary>
    public const int BytebufParamsFieldNumber = 1;
    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public global::Grpc.Testing.ByteBufferParams BytebufParams {
      get { return payloadCase_ == PayloadOneofCase.BytebufParams ? (global::Grpc.Testing.ByteBufferParams) payload_ : null; }
      set {
        payload_ = value;
        payloadCase_ = value == null ? PayloadOneofCase.None : PayloadOneofCase.BytebufParams;
      }
    }

    /// <summary>Field number for the "simple_params" field.</summary>
    public const int SimpleParamsFieldNumber = 2;
    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public global::Grpc.Testing.SimpleProtoParams SimpleParams {
      get { return payloadCase_ == PayloadOneofCase.SimpleParams ? (global::Grpc.Testing.SimpleProtoParams) payload_ : null; }
      set {
        payload_ = value;
        payloadCase_ = value == null ? PayloadOneofCase.None : PayloadOneofCase.SimpleParams;
      }
    }

    /// <summary>Field number for the "complex_params" field.</summary>
    public const int ComplexParamsFieldNumber = 3;
    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public global::Grpc.Testing.ComplexProtoParams ComplexParams {
      get { return payloadCase_ == PayloadOneofCase.ComplexParams ? (global::Grpc.Testing.ComplexProtoParams) payload_ : null; }
      set {
        payload_ = value;
        payloadCase_ = value == null ? PayloadOneofCase.None : PayloadOneofCase.ComplexParams;
      }
    }

    private object payload_;
    /// <summary>Enum of possible cases for the "payload" oneof.</summary>
    public enum PayloadOneofCase {
      None = 0,
      BytebufParams = 1,
      SimpleParams = 2,
      ComplexParams = 3,
    }
    private PayloadOneofCase payloadCase_ = PayloadOneofCase.None;
    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public PayloadOneofCase PayloadCase {
      get { return payloadCase_; }
    }

    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public void ClearPayload() {
      payloadCase_ = PayloadOneofCase.None;
      payload_ = null;
    }

    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public override bool Equals(object other) {
      return Equals(other as PayloadConfig);
    }

    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public bool Equals(PayloadConfig other) {
      if (ReferenceEquals(other, null)) {
        return false;
      }
      if (ReferenceEquals(other, this)) {
        return true;
      }
      if (!object.Equals(BytebufParams, other.BytebufParams)) return false;
      if (!object.Equals(SimpleParams, other.SimpleParams)) return false;
      if (!object.Equals(ComplexParams, other.ComplexParams)) return false;
      if (PayloadCase != other.PayloadCase) return false;
      return true;
    }

    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public override int GetHashCode() {
      int hash = 1;
      if (payloadCase_ == PayloadOneofCase.BytebufParams) hash ^= BytebufParams.GetHashCode();
      if (payloadCase_ == PayloadOneofCase.SimpleParams) hash ^= SimpleParams.GetHashCode();
      if (payloadCase_ == PayloadOneofCase.ComplexParams) hash ^= ComplexParams.GetHashCode();
      hash ^= (int) payloadCase_;
      return hash;
    }

    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public override string ToString() {
      return pb::JsonFormatter.ToDiagnosticString(this);
    }

    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public void WriteTo(pb::CodedOutputStream output) {
      if (payloadCase_ == PayloadOneofCase.BytebufParams) {
        output.WriteRawTag(10);
        output.WriteMessage(BytebufParams);
      }
      if (payloadCase_ == PayloadOneofCase.SimpleParams) {
        output.WriteRawTag(18);
        output.WriteMessage(SimpleParams);
      }
      if (payloadCase_ == PayloadOneofCase.ComplexParams) {
        output.WriteRawTag(26);
        output.WriteMessage(ComplexParams);
      }
    }

    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public int CalculateSize() {
      int size = 0;
      if (payloadCase_ == PayloadOneofCase.BytebufParams) {
        size += 1 + pb::CodedOutputStream.ComputeMessageSize(BytebufParams);
      }
      if (payloadCase_ == PayloadOneofCase.SimpleParams) {
        size += 1 + pb::CodedOutputStream.ComputeMessageSize(SimpleParams);
      }
      if (payloadCase_ == PayloadOneofCase.ComplexParams) {
        size += 1 + pb::CodedOutputStream.ComputeMessageSize(ComplexParams);
      }
      return size;
    }

    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public void MergeFrom(PayloadConfig other) {
      if (other == null) {
        return;
      }
      switch (other.PayloadCase) {
        case PayloadOneofCase.BytebufParams:
          if (BytebufParams == null) {
            BytebufParams = new global::Grpc.Testing.ByteBufferParams();
          }
          BytebufParams.MergeFrom(other.BytebufParams);
          break;
        case PayloadOneofCase.SimpleParams:
          if (SimpleParams == null) {
            SimpleParams = new global::Grpc.Testing.SimpleProtoParams();
          }
          SimpleParams.MergeFrom(other.SimpleParams);
          break;
        case PayloadOneofCase.ComplexParams:
          if (ComplexParams == null) {
            ComplexParams = new global::Grpc.Testing.ComplexProtoParams();
          }
          ComplexParams.MergeFrom(other.ComplexParams);
          break;
      }

    }

    [global::System.Diagnostics.DebuggerNonUserCodeAttribute]
    public void MergeFrom(pb::CodedInputStream input) {
      uint tag;
      while ((tag = input.ReadTag()) != 0) {
        switch(tag) {
          default:
            input.SkipLastField();
            break;
          case 10: {
            global::Grpc.Testing.ByteBufferParams subBuilder = new global::Grpc.Testing.ByteBufferParams();
            if (payloadCase_ == PayloadOneofCase.BytebufParams) {
              subBuilder.MergeFrom(BytebufParams);
            }
            input.ReadMessage(subBuilder);
            BytebufParams = subBuilder;
            break;
          }
          case 18: {
            global::Grpc.Testing.SimpleProtoParams subBuilder = new global::Grpc.Testing.SimpleProtoParams();
            if (payloadCase_ == PayloadOneofCase.SimpleParams) {
              subBuilder.MergeFrom(SimpleParams);
            }
            input.ReadMessage(subBuilder);
            SimpleParams = subBuilder;
            break;
          }
          case 26: {
            global::Grpc.Testing.ComplexProtoParams subBuilder = new global::Grpc.Testing.ComplexProtoParams();
            if (payloadCase_ == PayloadOneofCase.ComplexParams) {
              subBuilder.MergeFrom(ComplexParams);
            }
            input.ReadMessage(subBuilder);
            ComplexParams = subBuilder;
            break;
          }
        }
      }
    }

  }

  #endregion

}

#endregion Designer generated code
