import imageio.v2 as imageio
import os
import argparse
from PIL import Image
import numpy as np

def create_gif(input_folder, output_file, duration):
    # Ensure output file has the correct .gif extension
    if not output_file.lower().endswith('.gif'):
        output_file += '.gif'

    images = []
    for file_name in sorted(os.listdir(input_folder)):
        if file_name.endswith('.png'):
            file_path = os.path.join(input_folder, file_name)
            # Load the image using PIL
            img = Image.open(file_path)
            # Calculate new height preserving the aspect ratio
            width_percent = (600 / float(img.size[0]))
            hsize = int((float(img.size[1]) * float(width_percent)))
            # Resize the image
            img = img.resize((480, hsize), Image.LANCZOS)
            # Convert the image to a numpy array for imageio
            images.append(np.array(img))

    # Save the images as a gif, specify the format explicitly
    imageio.mimsave(output_file, images, format='GIF', duration=duration)

def main():
    parser = argparse.ArgumentParser(description="Create an animated GIF from a set of PNG images, resized to a width of 600 pixels.")
    parser.add_argument("input_folder", type=str, help="Directory containing PNG images")
    parser.add_argument("output_file", type=str, help="Path for the output GIF file")
    parser.add_argument("--duration", type=float, default=0.1, help="Duration of each frame in the GIF (in seconds)")

    args = parser.parse_args()

    create_gif(args.input_folder, args.output_file, args.duration)

if __name__ == "__main__":
    main()
