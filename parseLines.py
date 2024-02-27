def find_smallest_l1_aat(file_path):
    smallest_aat = None

    with open(file_path, 'r') as file:
        for line in file:
            if line.startswith("L1 average access time (AAT):"):
                # Extract the numeric value of AAT
                aat = float(line.split(":")[1].strip())
                
                # Initialize smallest_aat or compare with the current smallest
                if smallest_aat is None or aat < smallest_aat:
                    smallest_aat = aat

    return smallest_aat

# Replace 'test.log' with the actual path to your log file
file_path = 'test.log'
smallest_aat = find_smallest_l1_aat(file_path)
print(f"The smallest L1 average access time (AAT) is: {smallest_aat}")
