on run
    set appPath to POSIX path of (path to me)
    set runnerPath to appPath & "Contents/Resources/extract-assets-terminal.sh"
    tell application "Terminal"
        activate
        do script quoted form of runnerPath
    end tell
end run
