import subprocess
import os
import sys

def run_command(command):
    """Utility function to run a shell command and capture its output."""
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, shell=True)
    if result.returncode != 0:
        print("Error executing command:", ' '.join(command))
        print(result.stderr)
        sys.exit(1)
    return result.stdout

def clone_or_update_repo(repo_url, dest_dir, git_tag=None):
    """Clone or update a git repository at a specified tag or the latest commit."""
    # Check if the directory already exists and has a .git folder
    if os.path.isdir(os.path.join(dest_dir, '.git')):
        print(f"Repository already exists at {dest_dir}. Checking for updates...")
        # Fetch changes without applying them to check for new commits
        run_command(f"git -C {dest_dir} fetch")
        
        # Check if there are any new commits
        local_commit = run_command(f"git -C {dest_dir} rev-parse HEAD")
        remote_commit = run_command(f"git -C {dest_dir} " + "rev-parse @{u}")

        if local_commit.strip() != remote_commit.strip():
            print("Updates found. Pulling changes...")
            run_command(f"git -C {dest_dir} pull")
        else:
            print("No updates found.")
    else:
        # Directory does not exist, clone the repo
        print(f"Cloning repository {repo_url} into {dest_dir}...")
        run_command(f"git clone {repo_url} {dest_dir}")
    
    # If a git tag is specified, check it out
    if git_tag:
        print(f"Checking out tag {git_tag}...")
        run_command(f"git -C {dest_dir} checkout tags/{git_tag}")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python FetchGitRepo.py <repo_url> <destination_directory> [git_tag]")
        sys.exit(1)

    repo_url = sys.argv[1]
    destination_directory = sys.argv[2]
    git_tag = sys.argv[3] if len(sys.argv) > 3 else None

    clone_or_update_repo(repo_url, destination_directory, git_tag)
