from abc import ABCMeta, abstractmethod
from json_helper import JSON

class IConfig(metaclass=ABCMeta):
    @abstractmethod
    def configuration(self): raise NotImplementedError

class IAutostart(metaclass=ABCMeta):
    @abstractmethod
    def autostart(self): raise NotImplementedError

class IRoot(metaclass=ABCMeta):
    @abstractmethod
    def root(self): raise NotImplementedError
    
class IPreconditions(metaclass=ABCMeta):
    @abstractmethod
    def preconditions(self): raise NotImplementedError

