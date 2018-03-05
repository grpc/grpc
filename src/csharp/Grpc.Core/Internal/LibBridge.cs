namespace Grpc.Core.Internal {
  public enum LibBridgeType {
    UnmanagedLibrary, // default.
    Ext,
    Internal
  }
  public static class LibBridge {
    public static LibBridgeType type;
  }

}
