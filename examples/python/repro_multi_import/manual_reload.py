import importlib
import sys

# --- 1. Initial Import ---
print("--- Initial Import ---")

# Import grpc and print its initial state
import grpc

# Get the memory ID of the module object itself
module_id_before = id(grpc)
print(f"Module ID (before reload): {module_id_before}")

# Get the memory ID of a class *inside* the module
channel_class_before = grpc.Channel
class_id_before = id(channel_class_before)
print(f"grpc.Channel Class ID (before reload): {class_id_before}")

# This simulates another file doing 'from grpc import Channel'
# It's an "old reference" that won't be updated.
old_channel_reference = grpc.Channel

print("\n--- Manual Reload ---")
print("Executing 'importlib.reload(grpc)'...")


# # --- 2. The Reload ---
# # This re-runs all of gRPC's top-level code.
reloaded_grpc = importlib.reload(grpc)

print("'reload' complete.")

# --- 3. Analysis ---
print("\n--- After Reload Analysis ---")

# Check the module ID: It will be THE SAME.
module_id_after = id(reloaded_grpc)
print(f"Module ID (after reload):  {module_id_after}")
print(f"-> Module IDs are the same? {module_id_before == module_id_after}")

# Check the class ID: It will be DIFFERENT.
# reload created a new Channel class and replaced the old one.
channel_class_after = reloaded_grpc.Channel
class_id_after = id(channel_class_after)
print(f"\ngrpc.Channel Class ID (after reload):  {class_id_after}")
print(f"-> Class IDs are the same? {class_id_before == class_id_after}")


# --- 4. The "Danger" Demonstrated ---
print("\n--- The 'Danger' of Reloading ---")
print(f"ID of our 'old_channel_reference': {id(old_channel_reference)}")
print(f"ID of the 'reloaded_grpc.Channel': {id(reloaded_grpc.Channel)}")

# This will be False. They are two different class objects.
are_same = old_channel_reference is reloaded_grpc.Channel
print(f"\nIs the old reference the same as the new class? {are_same}")

# Create a new channel using the *reloaded* module
# This channel's type is the *new* grpc.Channel
new_channel_obj = reloaded_grpc.insecure_channel("localhost:12345")

print(f"\nCreated a new channel object: {type(new_channel_obj)}")

# This is where bugs hide. An `isinstance` check against
# the *old* class reference will fail!
print(
    f"isinstance(new_channel_obj, old_channel_reference)? {isinstance(new_channel_obj, old_channel_reference)}"
)

# The check against the *new* class reference will pass.
print(
    f"isinstance(new_channel_obj, reloaded_grpc.Channel)? {isinstance(new_channel_obj, reloaded_grpc.Channel)}"
)

new_channel_obj.close()
