#!/usr/bin/env python3

# A single token, probably an operator or value
class Token:
    # Some things that descend from Token are static only helpers,
    # this class attribute lets you know what they are
    static_only = False

    # Some special characters used as place holders for various values
    # These are all in the Unicode private space, so they shouldn't
    # occur in normal strings
    # Used to keep a space but prevent it from being tokenized
    SPACE = "\uE000"
    # An impossible character to hide some special conversion types
    UNPRINTABLE = "\uE001"
    # 10 characters used to store IDs of special values that are pre-processed
    SPECIAL = "\uE010\uE011\uE012\uE013\uE014\uE015\uE016\uE017\uE018\uE019"
    # All characters that are filtered out of the source string
    ALL = SPACE + UNPRINTABLE + SPECIAL

    def __init__(self, value):
        # Setup the basic stuff, including the prev and next
        # nodes for a linked list
        self.value = value
        self.next = None
        self.prev = None

    def get_desc(self):
        # This should be implemented by the child class
        raise NotImplementedError("Incomplete implementation of token type")

    def handles_lhs(self):
        # Does this token want something on the left hand side?
        # This used to know if we should synthesize a 'last' value if
        # no lhs value is present
        return False

    def is_types(self, *args):
        # Helper to see if a list of types matches this token
        # and the next ones
        cur = self
        for i in range(len(args)):
            if cur is None:
                return False
            if not isinstance(cur, args[i]):
                return False
            cur = cur.next
        return True

    def insert(self, new_value, from_value, to_value):
        # Insert a token in a linked list, replacing from 
        # from_value nodes before, and to_value nodes after
        # this token
        if new_value is None:
            if from_value.prev is not None:
                from_value.prev.next = to_value.next
                if new_value is None:
                    new_value = from_value.prev
            if to_value.next is not None:
                to_value.next.prev = to_value.prev
                if new_value is None:
                    new_value = to_value.next
            if new_value is None:
                return None, None
        else:
            new_value.prev = from_value.prev
            new_value.next = to_value.next
            if new_value.prev is not None:
                new_value.prev.next = new_value
            if new_value.next is not None:
                new_value.next.prev = new_value
        return new_value, new_value.get_head()
    
    def get_head(self):
        # Simple helper to return the head token in this list
        head = self
        while head.prev is not None:
            head = head.prev
        return head
    
    def __getitem__(self, key):
        # Return a token by index, can 0 returns this token
        # -1, returns the previous, 1 returns the next, and so on
        temp = self
        while key > 0:
            temp = temp.next
            key -= 1
        while key < 0:
            temp = temp.prev
            key += 1
        return temp

    def iter(self):
        # Iterate through all tokens, starting with this one
        cur = self
        while cur is not None:
            yield cur
            cur = cur.next

    def to_string(self):
        # Simple to_string implementation, probably want to override
        return "".join("_" if x in Token.ALL else x for x in str(self.value))

    def __repr__(self):
        # Override repr to make debugging a bit easier
        return f"{self.get_desc()}[{self.to_string()}]"

    def list_to_string(self):
        return ' '.join([x.to_string() for x in self.iter()])
