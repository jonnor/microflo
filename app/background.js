chrome.app.runtime.onLaunched.addListener(function() {
  chrome.app.window.create('../index.html', {
    id: 'microflo-ui',
    bounds: {
      width: 400,
      height: 600
    },
    resizable: false
  });
});
