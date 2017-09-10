# F# and gRPC

This project is nice to edit with [Ionide][ionide].

## macOS/OS X

```
brew update
brew cask install dotnet-sdk
```

Ensure you have `dotnet` as a binary on PATH, otherwise see where the .Net SDK installed from
`cat /etc/paths.d/dotnet`.

## Build

    dotnet restore
    dotnet build

## Run

In two terminals:

    dotnet run Server

and...

    dotnet run Client

 [ionide]: http://ionide.io/
