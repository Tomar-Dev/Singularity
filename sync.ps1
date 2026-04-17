# Singularity Smart Recovery & Sync Script - v1.4

$remote_url = "https://github.com/Tomar-Dev/Singularity.git"

# 1. .git klasörü yoksa tamir et
if (!(Test-Path ".git")) {
    Write-Host ">>> .git folder is missing! Recovering history from GitHub..." -ForegroundColor Cyan
    git init -q
    git remote add origin $remote_url
    git fetch origin main -q
    # Dal ismini GitHub ile aynı (main) yap
    git checkout -B main origin/main -q
    git reset --mixed origin/main -q
    Write-Host ">>> History recovered and branch set to 'main'." -ForegroundColor Green
}

# 2. Mevcut dalın isminin 'main' olduğundan emin ol (Hata önleyici)
git branch -M main

# 3. Değişiklikleri tara
git add .

# 4. Değişiklik var mı kontrol et (Varsa commit at)
$status = git status --porcelain
if ($status) {
    $date = Get-Date -Format "yyyy-MM-dd HH:mm"
    git commit -m "Auto-Sync: $date" -q
    Write-Host "Changes committed to local 'main' branch." -ForegroundColor Gray
}

# 5. GitHub'a gönder (Push)
Write-Host "Updating GitHub..." -ForegroundColor Cyan
# Hataları yakalamak için doğrudan komutu çalıştıralım
$pushError = git push origin main 2>&1

# Eğer geçmiş uyuşmazlığı varsa kullanıcıya sor
if ($LASTEXITCODE -ne 0) {
    Write-Host "Normal push failed (History mismatch)." -ForegroundColor Red
    $choice = Read-Host "Do you want to FORCE update GitHub? (y/n)"
    if ($choice -eq 'y') {
        Write-Host "Force pushing..." -ForegroundColor Yellow
        git push origin main --force
    }
}

Write-Host ">>> Sync process finished!" -ForegroundColor Green
pause