<!doctype html>
<html lang="<?= $e($lang) ?>">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta name="robots" content="noindex, nofollow">
  <meta name="theme-color" content="#f4f2ef" media="(prefers-color-scheme: light)">
  <meta name="theme-color" content="#0d1116" media="(prefers-color-scheme: dark)">
  <link rel="manifest" href="<?= $e($base) ?>/manifest.webmanifest">
  <link rel="apple-touch-icon" href="<?= $e($base) ?>/assets/icons/apple-touch-icon.png">
  <meta name="mobile-web-app-capable" content="yes">
  <meta name="apple-mobile-web-app-capable" content="yes">
  <meta name="apple-mobile-web-app-title" content="Hinasto">
  <meta name="apple-mobile-web-app-status-bar-style" content="default">
  <title><?= $e($title) ?> · Hinasto Controller</title>
  <meta name="base" content="<?= $e($base) ?>">
  <meta name="app-build" content="<?= $e($assetVersion) ?>">
  <link rel="icon" href="<?= $e($base) ?>/assets/favicon.svg" type="image/svg+xml">
  <link rel="stylesheet" href="<?= $e($base) ?>/assets/app.css?v=<?= $e($assetVersion) ?>">
</head>
<body class="<?= $e($bodyClass) ?>">
<?= $content ?>
<footer class="site-foot">Hinasto Controller · <?= $t('layout.server') ?> <?= $e($serverVersion) ?></footer>
<?php if ($script !== ''): ?>
  <script type="application/json" id="i18n"><?= $scriptStrings ?></script>
  <script src="<?= $e($base) ?>/assets/<?= $e($script) ?>?v=<?= $e($assetVersion) ?>"></script>
<?php endif; ?>
</body>
</html>
