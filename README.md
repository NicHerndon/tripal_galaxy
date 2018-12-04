# tripal_galaxy
This module is for integration of Tripal and the Galaxy Project

# Unit Testing
Testing of this module can be performed using a pre-configured suite of Docker images and Docker compose. From within the Tripal Galaxy project directory, execute the following to perform testing:

```bash
# Fire up the docker images
docker-compose up -d

# Install blend4php
docker-compose exec app bash -c 'cd /var/www/html/sites/all/libraries; git clone https://github.com/galaxyproject/blend4php.git'

# Enable the Tripal Galaxy module
docker-compose exec app bash -c "cd /var/www/html; drush en -y tripal_galaxy"

# Start the Unit tests
docker-compose exec app bash -c 'cd /modules/tripal_galaxy; ./vendor/bin/phpunit'
```

