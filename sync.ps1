# Stage all changes
git add .

# Prompt user for commit message
$msg = Read-Host "Enter commit message"
if ([string]::IsNullOrWhiteSpace($msg)) { $msg = "Minor updates" }

# Commit to local repository
git commit -m "$msg"

# Push to GitHub
Write-Host "Updating GitHub repository..." -ForegroundColor Cyan
git push origin main

Write-Host "Sync completed successfully!" -ForegroundColor Green
pause