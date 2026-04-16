# Değişiklikleri tara
git add .

# Kullanıcıdan commit mesajı iste
$msg = Read-Host "Güncelleme notu (Commit message) girin"
if ([string]::IsNullOrWhiteSpace($msg)) { $msg = "Minor updates" }

# Yerel depoya kaydet
git commit -m "$msg"

# GitHub'a gönder
Write-Host "GitHub güncelleniyor..." -ForegroundColor Cyan
git push origin main

# Codeberg'e gönder
Write-Host "Codeberg güncelleniyor..." -ForegroundColor Blue
git push codeberg main

Write-Host "İşlem başarıyla tamamlandı!" -ForegroundColor Green
pause