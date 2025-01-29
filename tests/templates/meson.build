template_files = [
  'access.py',
  'account.py',
  'appchooser.py',
  'background.py',
  'clipboard.py',
  'dynamiclauncher.py',
  'email.py',
  'filechooser.py',
  'geoclue2.py',
  'globalshortcuts.py',
  'inhibit.py',
  '__init__.py',
  'inputcapture.py',
  'lockdown.py',
  'notification.py',
  'print.py',
  'remotedesktop.py',
  'screenshot.py',
  'settings.py',
  'usb.py',
  'wallpaper.py',
]

fs = import('fs')

foreach template_file : template_files 
  fs.copyfile(template_file)
endforeach

if enable_installed_tests
  install_data(
    template_files,
    install_dir: installed_tests_dir / 'tests' / 'templates',
  )
endif
