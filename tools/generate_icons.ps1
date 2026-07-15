param([string]$OutputDirectory = (Join-Path $PSScriptRoot '..\host\resources'))
Add-Type -AssemblyName System.Drawing
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$sizes = 16,20,24,32,48,256
function New-Frame([int]$size,[string]$state){
  $bitmap=[Drawing.Bitmap]::new($size,$size,[Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $g=[Drawing.Graphics]::FromImage($bitmap);$g.SmoothingMode=if($size-le 24){[Drawing.Drawing2D.SmoothingMode]::None}else{[Drawing.Drawing2D.SmoothingMode]::AntiAlias};$g.Clear([Drawing.Color]::Transparent);$s=$size/16.0
  $outline=[Drawing.Pen]::new([Drawing.Color]::FromArgb(255,35,42,52),[Math]::Max(1,$s));$color=if($state-eq'disabled'){[Drawing.Color]::FromArgb(255,132,139,148)}else{[Drawing.Color]::FromArgb(255,70,135,245)};$fill=[Drawing.SolidBrush]::new($color)
  $g.FillRectangle($fill,[single](3*$s),[single](2*$s),[single](9*$s),[single](7*$s));$g.DrawRectangle($outline,[single](3*$s),[single](2*$s),[single](9*$s),[single](7*$s));$g.FillRectangle($fill,[single](5*$s),[single](6*$s),[single](9*$s),[single](7*$s));$g.DrawRectangle($outline,[single](5*$s),[single](6*$s),[single](9*$s),[single](7*$s))
  $arrow=[Drawing.Pen]::new([Drawing.Color]::White,[Math]::Max(1,$s));$g.DrawLine($arrow,[single](6*$s),[single](10*$s),[single](12*$s),[single](10*$s));$g.DrawLine($arrow,[single](10*$s),[single](8*$s),[single](12*$s),[single](10*$s));$g.DrawLine($arrow,[single](10*$s),[single](12*$s),[single](12*$s),[single](10*$s))
  if($state-ne'disabled'){$dotColor=if($state-eq'warning'){[Drawing.Color]::FromArgb(255,244,166,35)}else{[Drawing.Color]::FromArgb(255,38,196,104)};$dot=[Drawing.SolidBrush]::new($dotColor);$g.FillEllipse($dot,[single](1*$s),[single](11*$s),[single](4*$s),[single](4*$s));$g.DrawEllipse($outline,[single](1*$s),[single](11*$s),[single](4*$s),[single](4*$s));$dot.Dispose()}
  $arrow.Dispose();$fill.Dispose();$outline.Dispose();$g.Dispose();return $bitmap
}
function Write-Ico([string]$path,[string]$state){
  $frames=@();foreach($size in $sizes){$bitmap=New-Frame $size $state;$stream=[IO.MemoryStream]::new();$bitmap.Save($stream,[Drawing.Imaging.ImageFormat]::Png);$bitmap.Dispose();$frames+=,$stream.ToArray();$stream.Dispose()}
  $file=[IO.File]::Open($path,[IO.FileMode]::Create);$writer=[IO.BinaryWriter]::new($file);$writer.Write([uint16]0);$writer.Write([uint16]1);$writer.Write([uint16]$frames.Count);$offset=6+16*$frames.Count
  for($i=0;$i-lt$frames.Count;$i++){$size=$sizes[$i];$dimension=if($size-eq 256){0}else{$size};$writer.Write([byte]$dimension);$writer.Write([byte]$dimension);$writer.Write([byte]0);$writer.Write([byte]0);$writer.Write([uint16]1);$writer.Write([uint16]32);$writer.Write([uint32]$frames[$i].Length);$writer.Write([uint32]$offset);$offset+=$frames[$i].Length}
  foreach($frame in $frames){$writer.Write($frame)};$writer.Dispose();$file.Dispose()
}
Write-Ico (Join-Path $OutputDirectory 'icon-enabled.ico') 'enabled'
Write-Ico (Join-Path $OutputDirectory 'icon-disabled.ico') 'disabled'
Write-Ico (Join-Path $OutputDirectory 'icon-warning.ico') 'warning'
