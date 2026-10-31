<?php
declare(strict_types=1);

use Hinasto\Controllers\AnalysisController;
use Hinasto\Controllers\AuthController;
use Hinasto\Controllers\DeviceApiController;
use Hinasto\Controllers\FirmwareController;
use Hinasto\Controllers\PanelApiController;
use Hinasto\Controllers\PanelController;
use Hinasto\Controllers\PwaController;

// [METHOD, path, [Controller::class, 'method'], guards]   guards: 'auth' (logged in), 'csrf' (X-CSRF header)
return [
    ['GET',  '/',                  [PanelController::class, 'index']],
    ['GET',  '/manifest.webmanifest', [PwaController::class, 'manifest']],

    // controllers (ESP32)
    ['POST', '/api/v1/sync',       [DeviceApiController::class, 'sync']],
    ['GET',  '/api/v1/firmware',   [FirmwareController::class, 'download']],  // before languages existed: Polish
    ['GET',  '/api/v1/firmware-pl', [FirmwareController::class, 'download']],
    ['GET',  '/api/v1/firmware-en', [FirmwareController::class, 'download']],
    ['GET',  '/api/v1/firmware-de', [FirmwareController::class, 'download']],
    ['GET',  '/api/v1/firmware-cs', [FirmwareController::class, 'download']],
    ['GET',  '/api/v1/firmware-sk', [FirmwareController::class, 'download']],

    // panel
    ['POST', '/api/panel/login',   [AuthController::class, 'login']],
    ['POST', '/api/panel/logout',  [AuthController::class, 'logout'],       ['auth', 'csrf']],
    ['GET',  '/api/panel/state',   [PanelApiController::class, 'state'],    ['auth']],
    ['GET',  '/api/panel/firmware', [FirmwareController::class, 'overview'], ['auth']],
    ['POST', '/api/panel/firmware', [FirmwareController::class, 'upload'],   ['auth', 'csrf']],
    ['GET',  '/api/panel/enroll',  [PanelApiController::class, 'enroll'],   ['auth']],
    ['POST', '/api/panel/device', [PanelApiController::class, 'device'],   ['auth', 'csrf']],
    ['POST', '/api/panel/command', [PanelApiController::class, 'command'],  ['auth', 'csrf']],
    ['GET',  '/api/panel/log',     [PanelApiController::class, 'log'],      ['auth']],
    ['GET',  '/api/panel/analysis', [AnalysisController::class, 'analysis'], ['auth']],
    ['GET',  '/api/panel/runs',    [PanelApiController::class, 'runs'],     ['auth']],
    ['GET',  '/api/panel/history', [PanelApiController::class, 'history'],  ['auth']],
];
