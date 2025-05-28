import random

# Define the restricted cells
restricted_timeslots = {0, 20, 5, 50, 80, 90}

# Define the number of cells to generate
NUM_CELLS = 40

# Generate unique random cells, ensuring they are not in restricted_cells
generated_cells = {}
while len(generated_cells) < NUM_CELLS:
    timeslot_offset = random.randint(1, 101)  # Adjust as needed
    
    # Ensure the timeslot is unique
    if timeslot_offset in generated_cells:
        continue

    channel_offset = random.randint(0, 3)  # Adjust as needed
    cell = (timeslot_offset, channel_offset)

    if timeslot_offset not in restricted_timeslots:
        generated_cells[timeslot_offset] = channel_offset  # Store in dict to enforce unique timeslot


# Write to variables.h
with open("network_interference_cells.c", "w") as f:
    f.write(f"#include \"net/mac/tsch/sixtop/sixtop.h\"\n")
    f.write("#include \"network_interference_cells.h\"\n")
    f.write("#include \"contiki.h\"\n")
    f.write("#include \"sf-simple.h\"\n\n")
    f.write(f"sf_simple_cell_t network_interfere_cells[{len(generated_cells)}] = {{\n")
    
    for timeslot, channel in sorted(generated_cells.items()):
        f.write(f"    {{{timeslot}, {channel}}},\n")
    
    f.write("};\n\n")

print(f"Generated {len(generated_cells)} cells and wrote to network_interference_cells.c.")