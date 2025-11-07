console.log('Modern C Web Library - Static file serving is working!');

// Add some interactivity
document.addEventListener('DOMContentLoaded', function() {
    console.log('Page loaded successfully');
    
    // Log when links are clicked
    const links = document.querySelectorAll('a');
    links.forEach(link => {
        link.addEventListener('click', function(e) {
            console.log('Navigating to:', this.href);
        });
    });
});
