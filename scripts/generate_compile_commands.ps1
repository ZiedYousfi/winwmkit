param(
    [string]$RepoRoot = (Join-Path $PSScriptRoot ".."),
    [string]$Sanitizers = ""
)

$resolvedRoot = (Resolve-Path $RepoRoot).Path
$includeDir = Join-Path $resolvedRoot "include"
$entries = [System.Collections.Generic.List[object]]::new()

function Add-CompileCommand {
    param(
        [string]$Directory,
        [string]$SourceFile,
        [string]$ObjectFile,
        [string[]]$ExtraFlags = @()
    )

    $arguments = [System.Collections.Generic.List[string]]::new()
    $arguments.Add("cl.exe")
    $arguments.Add("/I$includeDir")
    $arguments.Add("/nologo")
    $arguments.Add("/W4")

    if ($Sanitizers) {
        $arguments.Add("/fsanitize=$Sanitizers")
    }

    foreach ($flag in $ExtraFlags) {
        $arguments.Add($flag)
    }

    $arguments.Add("/c")
    $arguments.Add($SourceFile)
    $arguments.Add("/Fo$ObjectFile")

    $entries.Add([ordered]@{
        directory = $Directory
        arguments = $arguments
        file = $SourceFile
        output = $ObjectFile
    })
}

$srcDir = Join-Path $resolvedRoot "src"
foreach ($source in Get-ChildItem -Path $srcDir -Filter "*.c" | Sort-Object Name) {
    $objectFile = Join-Path $resolvedRoot ("build\msvc\lib\{0}.obj" -f [System.IO.Path]::GetFileNameWithoutExtension($source.Name))
    Add-CompileCommand `
        -Directory $resolvedRoot `
        -SourceFile $source.FullName `
        -ObjectFile $objectFile `
        -ExtraFlags @("/DWWMK_BUILD_DLL")
}

$exampleSource = Join-Path $resolvedRoot "exemple\main.c"
if (Test-Path $exampleSource) {
    $exampleDirectory = Split-Path $exampleSource -Parent
    $exampleObject = Join-Path $resolvedRoot "exemple\build\msvc\main.obj"
    Add-CompileCommand `
        -Directory $exampleDirectory `
        -SourceFile $exampleSource `
        -ObjectFile $exampleObject
}

$targetPath = Join-Path $resolvedRoot "compile_commands.json"
$entries | ConvertTo-Json -Depth 4 | Set-Content -Path $targetPath -Encoding ascii
