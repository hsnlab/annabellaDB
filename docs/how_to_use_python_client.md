In this description, we are going to show, how you could ABDB in your python application, and enjoy the benefits offered by it. 

We assume that you already create and run your ABDB server cluster, so by now everything is ready for use :) 
To see how to create ABDB server cluster, please read this: [install ABDB on your server cluster](https://github.com/hsnlab/annabellaDB/blob/master/docs/install_abdb_on_server_cluster.md)

# Install python client for ABDB

```
 cd client/python/
 ./compile.sh
 ./install_dependencies.sh
 sudo python setup.py install
```

Now you're ready to use ABDB from your python source code. In the followings we show how cloud you import ABDB client and PUT or GET key-value pairs to your ABDB cluster.
