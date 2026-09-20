param(
    [Parameter(Mandatory = $true)][string]$ObsConfig,
    [Parameter(Mandatory = $true)][string]$DeviceSerial,
    [Parameter(Mandatory = $true)][string]$OutputPath,
    [int]$CaptureWaitSeconds = 8
)

$ErrorActionPreference = 'Stop'
$websocketConfig = Get-Content -LiteralPath $ObsConfig -Raw | ConvertFrom-Json
if (-not $websocketConfig.server_enabled) {
    throw 'OBS WebSocket server is disabled'
}

$client = [System.Net.WebSockets.ClientWebSocket]::new()
$script:requestNumber = 0
$createdInputName = $null
$result = [ordered]@{
    status = 'FAIL'
    checked_at = (Get-Date).ToString('o')
    port = $websocketConfig.server_port
    auth_required = [bool]$websocketConfig.auth_required
    device_serial = $DeviceSerial
    temporary_input = $null
    input_active = $null
    capture_property_items = @()
    cleanup = 'NOT_ATTEMPTED'
}

function Send-Message($message) {
    $bytes = [Text.Encoding]::UTF8.GetBytes(($message | ConvertTo-Json -Depth 20 -Compress))
    $segment = [ArraySegment[byte]]::new($bytes)
    [void]$client.SendAsync($segment, [System.Net.WebSockets.WebSocketMessageType]::Text, $true, [Threading.CancellationToken]::None).GetAwaiter().GetResult()
}

function Receive-Message([int]$TimeoutSeconds = 15) {
    $buffer = [byte[]]::new(65536)
    $stream = [IO.MemoryStream]::new()
    $timeout = [Threading.CancellationTokenSource]::new([TimeSpan]::FromSeconds($TimeoutSeconds))
    try {
        do {
            $segment = [ArraySegment[byte]]::new($buffer)
            $response = $client.ReceiveAsync($segment, $timeout.Token).GetAwaiter().GetResult()
            if ($response.MessageType -eq [System.Net.WebSockets.WebSocketMessageType]::Close) {
                throw 'OBS WebSocket closed unexpectedly'
            }
            $stream.Write($buffer, 0, $response.Count)
        } while (-not $response.EndOfMessage)
        return ([Text.Encoding]::UTF8.GetString($stream.ToArray()) | ConvertFrom-Json)
    } finally {
        $timeout.Dispose()
        $stream.Dispose()
    }
}

function Invoke-Request([string]$RequestType, $RequestData = @{}) {
    $script:requestNumber++
    $requestId = "phantom-display-$script:requestNumber"
    Send-Message @{ op = 6; d = @{ requestType = $RequestType; requestId = $requestId; requestData = $RequestData } }
    do {
        $response = Receive-Message
    } while ($response.op -ne 7 -or $response.d.requestId -ne $requestId)
    if (-not $response.d.requestStatus.result) {
        $status = $response.d.requestStatus
        throw "$RequestType failed: code=$($status.code) comment=$($status.comment)"
    }
    return $response.d.responseData
}

try {
    $connectTimeout = [Threading.CancellationTokenSource]::new([TimeSpan]::FromSeconds(15))
    try {
        [void]$client.ConnectAsync([Uri]("ws://127.0.0.1:" + $websocketConfig.server_port), $connectTimeout.Token).GetAwaiter().GetResult()
    } finally {
        $connectTimeout.Dispose()
    }
    $hello = Receive-Message
    if ($hello.op -ne 0) {
        throw "Expected OBS WebSocket Hello, got op=$($hello.op)"
    }
    $identify = @{ rpcVersion = 1; eventSubscriptions = 0 }
    if ($null -ne $hello.d.authentication) {
        # The secret stays in memory and is never written to the result or console.
        $salted = [Text.Encoding]::UTF8.GetBytes([string]$websocketConfig.server_password + $hello.d.authentication.salt)
        $secret = [Convert]::ToBase64String([Security.Cryptography.SHA256]::HashData($salted))
        $challenge = [Text.Encoding]::UTF8.GetBytes($secret + $hello.d.authentication.challenge)
        $identify.authentication = [Convert]::ToBase64String([Security.Cryptography.SHA256]::HashData($challenge))
    }
    Send-Message @{ op = 1; d = $identify }
    $identified = Receive-Message
    if ($identified.op -ne 2) {
        throw "OBS WebSocket identification failed: op=$($identified.op)"
    }

    $version = Invoke-Request 'GetVersion'
    $sceneList = Invoke-Request 'GetSceneList'
    $createdInputName = 'codex-phantom-display-' + (Get-Date -Format 'yyyyMMdd-HHmmss')
    $result.temporary_input = $createdInputName
    $created = Invoke-Request 'CreateInput' @{
        sceneName = $sceneList.currentProgramSceneName
        inputName = $createdInputName
        inputKind = 'srccpy_source'
        inputSettings = @{
            device_list = $DeviceSerial
            choose_src = 0
            choose_capture = '0'
            choose_res = '1920x1080'
            choose_fps = 30
            audio_enable = $false
            wifi_pair = $false
            pair_info = ''
        }
        sceneItemEnabled = $true
    }
    Start-Sleep -Seconds $CaptureWaitSeconds
    $active = Invoke-Request 'GetSourceActive' @{ sourceName = $createdInputName }
    $result.input_active = [bool]$active.videoActive
    if (-not $result.input_active) {
        throw 'Temporary screen capture did not become video-active'
    }
    # Requesting the property list creates the plugin properties and refreshes device data.
    $captureList = Invoke-Request 'GetInputPropertiesListPropertyItems' @{ inputName = $createdInputName; propertyName = 'choose_capture' }
    $result.capture_property_items = @($captureList.propertyItems | ForEach-Object {
        [ordered]@{ name = $_.itemName; value = [string]$_.itemValue }
    })
    if ($result.capture_property_items.Count -ne 1 -or $result.capture_property_items[0].value -ne '0') {
        throw 'Capture list contains an unexpected display candidate while the screen capture is active'
    }
    $result.obs_version = $version.obsVersion
    $result.websocket_version = $version.obsWebSocketVersion
    $result.status = 'PASS'
} catch {
    $result.error = $_.Exception.Message
} finally {
    if ($null -ne $createdInputName -and $client.State -eq [System.Net.WebSockets.WebSocketState]::Open) {
        try {
            Invoke-Request 'RemoveInput' @{ inputName = $createdInputName } | Out-Null
            $result.cleanup = 'REMOVED_TEMPORARY_INPUT'
        } catch {
            $result.cleanup = 'FAILED: ' + $_.Exception.Message
        }
    }
    if ($client.State -eq [System.Net.WebSockets.WebSocketState]::Open) {
        [void]$client.CloseAsync([System.Net.WebSockets.WebSocketCloseStatus]::NormalClosure, 'phantom display verification complete', [Threading.CancellationToken]::None).GetAwaiter().GetResult()
    }
    $client.Dispose()
    $result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $OutputPath -Encoding utf8
}

$result | ConvertTo-Json -Depth 8
if ($result.status -ne 'PASS') {
    throw 'Phantom display runtime verification failed'
}
