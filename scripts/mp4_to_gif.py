import argparse
from moviepy.editor import VideoFileClip
from PIL import Image

def process_frame(frame, xmin_pct, xmax_pct, ymin_pct, ymax_pct, output_width, output_height):
    # Convert the frame to a PIL image
    img = Image.fromarray(frame)
    
    # Calculate cropping coordinates based on percentages
    width, height = img.size
    x_min = int(xmin_pct * width / 100)
    x_max = int(xmax_pct * width / 100)
    y_min = int(ymin_pct * height / 100)
    y_max = int(ymax_pct * height / 100)
    
    # Crop the image
    cropped_img = img.crop((x_min, y_min, x_max, y_max))
    
    # Resize the cropped image to the specified dimensions
    resized_img = cropped_img.resize((output_width, output_height), Image.ANTIALIAS)
    
    return resized_img

def create_gif(input_video, output_gif, fps, xmin_pct, xmax_pct, ymin_pct, ymax_pct, output_width, output_height, max_frames=None):
    clip = VideoFileClip(input_video)
    frames = []
    
    frame_count = 0
    for frame in clip.iter_frames(fps=fps):
        processed_frame = process_frame(frame, xmin_pct, xmax_pct, ymin_pct, ymax_pct, output_width, output_height)
        frames.append(processed_frame)
        frame_count += 1
        if max_frames is not None and frame_count >= max_frames:
            break

    # Save the frames as a GIF
    frames[0].save(output_gif, save_all=True, append_images=frames[1:], loop=0, duration=int(1000 / fps))

def main():
    parser = argparse.ArgumentParser(description='Convert MP4 to GIF with cropping, scaling, and frame rate options.')
    parser.add_argument('input_video', type=str, help='Path to the input MP4 video.')
    parser.add_argument('output_gif', type=str, help='Path to save the output GIF.')
    parser.add_argument('--fps', type=int, default=10, help='Frame rate of the output GIF.')
    parser.add_argument('--xmin_pct', type=float, default=0.0, help='Minimum X percentage for cropping (from the left edge).')
    parser.add_argument('--xmax_pct', type=float, default=100.0, help='Maximum X percentage for cropping (from the left edge).')
    parser.add_argument('--ymin_pct', type=float, default=0.0, help='Minimum Y percentage for cropping (from the top edge).')
    parser.add_argument('--ymax_pct', type=float, default=100.0, help='Maximum Y percentage for cropping (from the top edge).')
    parser.add_argument('--output_width', type=int, required=True, help='Width of the output GIF.')
    parser.add_argument('--output_height', type=int, required=True, help='Height of the output GIF.')
    parser.add_argument('--max_frames', type=int, default=None, help='Max frames in the GIF.')

    args = parser.parse_args()

    create_gif(args.input_video, args.output_gif, args.fps, args.xmin_pct, args.xmax_pct, args.ymin_pct, args.ymax_pct, 
                    args.output_width, args.output_height, args.max_frames)

if __name__ == '__main__':
    main()
